// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/browser/tab_organization/tab_organization_controller.h"

#include <algorithm>
#include <set>
#include <utility>

#include "base/auto_reset.h"
#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/strings/utf_string_conversions.h"
#include "base/supports_user_data.h"
#include "base/task/sequenced_task_runner.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/sessions/session_service.h"
#include "chrome/browser/sessions/session_service_factory.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/grit/generated_resources.h"
#include "components/prefs/pref_service.h"
#include "components/sessions/content/session_tab_helper.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/storage_partition.h"
#include "content/public/browser/web_contents.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/menus/simple_menu_model.h"
#include "url/gurl.h"
#include "uw/browser/tab_organization/ai_connection.h"
#include "uw/browser/tab_organization/api_key_store.h"
#include "uw/components/tab_organization/ai_protocol.h"

namespace uw {
namespace {

// Carries organization with a page during native cross-window dragging. The
// persisted copy is in Chromium's session service, not a second profile file.
class PageTreeState : public base::SupportsUserData::Data {
 public:
  base::DictValue state;
};
const char kPageTreeStateKey = 0;

}  // namespace

DEFINE_USER_DATA(TabOrganizationController);

TabOrganizationController::TabOrganizationController(
    BrowserWindowInterface* window)
    : window_(window),
      tabs_(window->GetTabStripModel()),
      tree_(l10n_util::GetStringUTF16(IDS_UW_SCRATCHPAD)),
      coordinator_(&tree_),
      user_data_(window->GetUnownedUserDataHost(), *this) {
  tree_subscription_ = tree_.Observe(base::BindRepeating(
      &TabOrganizationController::ScheduleUpdate, weak_factory_.GetWeakPtr()));
  pref_registrar_.Init(window_->GetProfile()->GetPrefs());
  pref_registrar_.Add(
      kJevEndpointPref,
      base::BindRepeating(&TabOrganizationController::OnConfigChanged,
                          base::Unretained(this)));
  pref_registrar_.Add(
      kJevKeyAccountPref,
      base::BindRepeating(&TabOrganizationController::OnConfigChanged,
                          base::Unretained(this)));
  pref_registrar_.Add(
      kNamingEndpointPref,
      base::BindRepeating(&TabOrganizationController::OnConfigChanged,
                          base::Unretained(this)));
  pref_registrar_.Add(
      kNamingModelPref,
      base::BindRepeating(&TabOrganizationController::OnConfigChanged,
                          base::Unretained(this)));
  pref_registrar_.Add(
      kNamingKeyAccountPref,
      base::BindRepeating(&TabOrganizationController::OnConfigChanged,
                          base::Unretained(this)));
  pref_registrar_.Add(
      kOrganizationEnabledPref,
      base::BindRepeating(&TabOrganizationController::OnConfigChanged,
                          base::Unretained(this)));
  tabs_->AddObserver(this);
  Sync();
}

TabOrganizationController::~TabOrganizationController() {
  CancelAutomatic();
  tabs_->RemoveObserver(this);
}

void TabOrganizationController::OnConfigChanged() {
  if (!AutomaticEnabled()) {
    CancelAutomatic();
  } else {
    MaybeScheduleAutomatic();
  }
}

TabOrganizationController* TabOrganizationController::From(
    BrowserWindowInterface* window) {
  return window ? Get(window->GetUnownedUserDataHost()) : nullptr;
}

TabOrganizationController* TabOrganizationController::From(
    TabStripModel* model) {
  auto* tab = model ? model->GetActiveTab() : nullptr;
  return tab ? From(tab->GetBrowserWindowInterface()) : nullptr;
}

Profile* TabOrganizationController::profile() const {
  return window_->GetProfile();
}

bool TabOrganizationController::Eligible(tabs::TabInterface* tab) const {
  if (!tab || tabs_->GetIndexOfTab(tab) == TabStripModel::kNoTab)
    return false;
  // Existing native groups/splits retain their own views and controls. They are
  // never silently dissolved to create uw hierarchy metadata.
  return !tab->IsPinned() && !tab->GetGroup() &&
         !tabs_->GetSplitForTab(tabs_->GetIndexOfTab(tab));
}

std::optional<TreeId> TabOrganizationController::IdFor(
    const tabs::TabInterface* tab) const {
  if (!tab)
    return std::nullopt;
  auto it = pages_.find(tab->GetHandle());
  return it == pages_.end() ? std::nullopt : std::make_optional(it->second);
}

tabs::TabInterface* TabOrganizationController::TabFor(const TreeId& id) const {
  for (const auto& [handle, page] : pages_) {
    if (page == id && Eligible(handle.Get()))
      return handle.Get();
  }
  return nullptr;
}

void TabOrganizationController::Sync() {
  std::set<tabs::TabHandle> live;
  for (int i = 0; i < tabs_->count(); ++i) {
    auto* tab = tabs_->GetTabAtIndex(i);
    if (Eligible(tab))
      live.insert(tab->GetHandle());
  }
  std::vector<TreeId> removed;
  for (auto it = pages_.begin(); it != pages_.end();) {
    if (live.contains(it->first)) {
      ++it;
      continue;
    }
    removed.push_back(it->second);
    tree_.RemovePage(it->second);
    it = pages_.erase(it);
  }
  if (!removed.empty()) {
    coordinator_.Invalidate(removed);
  }
  std::vector<TreeId> fresh;
  for (int i = 0; i < tabs_->count(); ++i) {
    auto* tab = tabs_->GetTabAtIndex(i);
    if (!Eligible(tab))
      continue;
    auto* contents = tab->GetContents();
    if (!contents)
      continue;
    if (auto id = IdFor(tab)) {
      tree_.UpdatePageTitle(*id, contents->GetTitle());
      continue;
    }
    auto* saved =
        static_cast<PageTreeState*>(contents->GetUserData(&kPageTreeStateKey));
    auto id = saved ? tree_.RestorePage(saved->state, contents->GetTitle())
                    : tree_.AddPage(contents->GetTitle());
    pages_.emplace(tab->GetHandle(), id);
    if (!saved) {
      fresh.push_back(id);
    }
  }
  // A lone new tab without a source link (Cmd+T, menus, extensions) opens
  // directly beneath the previously active tab as its sibling instead of
  // piling onto Scratchpad. Restored sessions and bulk opens keep their
  // saved placement.
  if (fresh.size() == 1 && last_active_) {
    if (tree_.PlaceBeside(fresh[0], *last_active_)) {
      AlignTabOrder();
    }
  }
  if (auto* active = tabs_->GetActiveTab()) {
    if (auto id = IdFor(active)) {
      last_active_ = *id;
    }
  }
  tree_.ResolveRestoredParents();
  TrackActivityAndSchedule();
  ScheduleUpdate();
}

void TabOrganizationController::DidOpenLink(BrowserWindowInterface* window,
                                            content::WebContents* page,
                                            content::WebContents* source) {
  auto* self = From(window);
  if (!self || !source || page == source)
    return;
  self->Sync();
  auto child = self->IdFor(self->tabs_->GetTabForWebContents(page));
  auto parent = self->IdFor(self->tabs_->GetTabForWebContents(source));
  if (child && parent) {
    self->tree_.SetPageParent(*child, *parent);
    // Called after native insertion, outside its observer notifications.
    self->AlignTabOrder();
  }
}

void TabOrganizationController::RestoreTab(
    BrowserWindowInterface* window,
    content::WebContents* page,
    const std::map<std::string, std::string>& extra_data) {
  auto* self = From(window);
  auto it = extra_data.find(kSessionKey);
  if (!self || !page || it == extra_data.end())
    return;
  auto state = base::JSONReader::ReadDict(it->second, base::JSON_PARSE_RFC);
  auto* tab = self->tabs_->GetTabForWebContents(page);
  if (!state || !self->Eligible(tab))
    return;
  if (auto id = self->IdFor(tab))
    self->tree_.RemovePage(*id);
  self->pages_.erase(tab->GetHandle());
  self->pages_.emplace(tab->GetHandle(),
                       self->tree_.RestorePage(*state, page->GetTitle()));
  self->tree_.ResolveRestoredParents();
}

std::vector<TreeId> TabOrganizationController::SelectionFor(
    const tabs::TabInterface* context) const {
  std::vector<TreeId> result;
  if (context && tabs_->GetIndexOfTab(context) == TabStripModel::kNoTab)
    return {};
  if (context && !tabs_->IsTabSelected(tabs_->GetIndexOfTab(context))) {
    if (auto id = IdFor(context))
      result.push_back(*id);
    return result;
  }
  for (int index :
       tabs_->selection_model().GetListSelectionModel().selected_indices()) {
    auto id = IdFor(tabs_->GetTabAtIndex(index));
    if (!id)
      return {};  // Do not silently act on only part of a mixed selection.
    result.push_back(*id);
  }
  return tree_.Normalize(result);
}

std::optional<TreeId> TabOrganizationController::MakeTask(
    const std::vector<TreeId>& ids,
    std::u16string title) {
  coordinator_.Invalidate(ids);
  auto result = tree_.MakeTask(ids, std::move(title));
  AlignTabOrder();
  return result;
}

bool TabOrganizationController::Move(const std::vector<TreeId>& ids,
                                     const TreeId& parent,
                                     std::optional<TreeId> before) {
  coordinator_.Invalidate(ids);
  if (!tree_.Move(ids, parent, before))
    return false;
  AlignTabOrder();
  return true;
}

void TabOrganizationController::Rename(const TreeId& id, std::u16string title) {
  coordinator_.Invalidate({id});
  tree_.RenameTask(id, std::move(title));
}

std::vector<tabs::TabInterface*> TabOrganizationController::ResolvePages(
    const std::vector<TreeId>& ids) {
  std::vector<tabs::TabInterface*> pages;
  for (const auto& id : ids) {
    if (auto* tab = TabFor(id))
      pages.push_back(tab);
  }
  return pages;
}

void TabOrganizationController::CloseBranch(const std::vector<TreeId>& ids) {
  coordinator_.Invalidate(ids);
  const auto pages = tree_.PagesIn(ids);
  if (pages.empty()) {
    for (const auto& id : ids)
      tree_.RemoveEmptyTask(id);
    return;
  }
  // Closing the final page can destroy this controller. Do not access members
  // after this call. Re-resolve handles if beforeunload delays the operation.
  tabs_->ExecuteCloseTabsCommand(
      base::BindRepeating(
          [](base::WeakPtr<TabOrganizationController> controller,
             std::vector<TreeId> pages) {
            return controller ? controller->ResolvePages(pages)
                              : std::vector<tabs::TabInterface*>();
          },
          weak_factory_.GetWeakPtr(), pages),
      false);
}

void TabOrganizationController::Undo() {
  coordinator_.InvalidateAll();
  tree_.Undo();
  AlignTabOrder();
}

std::optional<std::u16string> TabOrganizationController::TakeNotice() {
  return std::exchange(notice_, std::nullopt);
}

bool TabOrganizationController::IsCommand(int command) {
  return command >= kNewTask && command <= kLastCommand;
}

void TabOrganizationController::AppendCommands(ui::SimpleMenuModel* menu) {
  menu->AddItem(kNewTask, l10n_util::GetStringUTF16(IDS_UW_NEW_TASK));
  menu->AddItem(kScratchpad,
                l10n_util::GetStringUTF16(IDS_UW_MOVE_TO_SCRATCHPAD));
  menu->AddItem(kMoveUp, l10n_util::GetStringUTF16(IDS_UW_MOVE_UP));
  menu->AddItem(kMoveDown, l10n_util::GetStringUTF16(IDS_UW_MOVE_DOWN));
  menu->AddItem(kNest, l10n_util::GetStringUTF16(IDS_UW_NEST_BRANCH));
  menu->AddItem(kOutdent, l10n_util::GetStringUTF16(IDS_UW_OUTDENT_BRANCH));
  menu->AddItem(kToggleCollapsed,
                l10n_util::GetStringUTF16(IDS_UW_TOGGLE_BRANCH));
  menu->AddItem(kUndo, l10n_util::GetStringUTF16(IDS_UW_UNDO_ORGANIZATION));
  menu->AddSeparator(ui::NORMAL_SEPARATOR);
  menu->AddItem(kCloseBranch, l10n_util::GetStringUTF16(IDS_UW_CLOSE_BRANCH));
}

std::optional<TabOrganizationController::MoveTarget>
TabOrganizationController::GetMoveTarget(int command, const TreeId& id) const {
  const auto* node = tree_.Find(id);
  if (!node || !node->parent())
    return std::nullopt;
  const auto* parent = node->parent();
  const auto& siblings = parent->children();
  const size_t index = parent->GetIndexOf(node).value();
  MoveTarget target{parent->value.id, std::nullopt};
  switch (command) {
    case kMoveUp:
      if (index == 0)
        return std::nullopt;
      target.before = siblings[index - 1]->value.id;
      if (siblings[index - 1]->value.kind == TabTree::Kind::kScratchpad)
        return std::nullopt;
      break;
    case kMoveDown:
      if (index + 1 >= siblings.size())
        return std::nullopt;
      if (index + 2 < siblings.size())
        target.before = siblings[index + 2]->value.id;
      break;
    case kNest:
      if (index == 0)
        return std::nullopt;
      target.parent = siblings[index - 1]->value.id;
      break;
    case kOutdent: {
      const auto* grandparent = parent->parent();
      if (!grandparent || parent == tree_.scratchpad())
        return std::nullopt;
      // A page leaving a root-level task returns to Scratchpad.
      target.parent = grandparent == tree_.root() &&
                              node->value.kind == TabTree::Kind::kPage
                          ? tree_.scratchpad()->value.id
                          : grandparent->value.id;
      if (target.parent == grandparent->value.id) {
        const size_t next = grandparent->GetIndexOf(parent).value() + 1;
        if (next < grandparent->children().size())
          target.before = grandparent->children()[next]->value.id;
      }
      break;
    }
    default:
      return std::nullopt;
  }
  return tree_.CanMove({id}, target.parent) ? std::make_optional(target)
                                            : std::nullopt;
}

bool TabOrganizationController::CanExecute(
    int command,
    const std::vector<TreeId>& ids) const {
  const auto roots = tree_.Normalize(ids);
  if (command == kUndo)
    return tree_.CanUndo();
  if (roots.empty())
    return false;
  switch (command) {
    case kNewTask:
      return std::ranges::all_of(roots, [this](const TreeId& id) {
        return tree_.Find(id)->value.kind == TabTree::Kind::kPage;
      });
    case kCloseBranch:
      return true;
    case kScratchpad:
      return tree_.CanMove(roots, tree_.scratchpad()->value.id);
    case kRenameTask:
      return roots.size() == 1 &&
             tree_.Find(roots[0])->value.kind == TabTree::Kind::kTask;
    case kToggleCollapsed:
      return roots.size() == 1 && !tree_.Find(roots[0])->children().empty();
    default:
      return roots.size() == 1 && GetMoveTarget(command, roots[0]).has_value();
  }
}

std::optional<TreeId> TabOrganizationController::Execute(
    int command,
    const std::vector<TreeId>& ids) {
  if (!CanExecute(command, ids))
    return std::nullopt;
  const auto roots = tree_.Normalize(ids);
  switch (command) {
    case kNewTask:
      return MakeTask(roots, l10n_util::GetStringUTF16(IDS_UW_UNTITLED_TASK));
    case kCloseBranch:
      CloseBranch(roots);
      return std::nullopt;
    case kUndo:
      Undo();
      break;
    case kScratchpad:
      Move(roots, tree_.scratchpad()->value.id);
      break;
    case kToggleCollapsed:
      tree_.ToggleCollapsed(roots[0]);
      break;
    default:
      if (auto target = GetMoveTarget(command, roots[0]))
        Move(roots, target->parent, target->before);
      break;
  }
  return std::nullopt;
}

void TabOrganizationController::AlignTabOrder() {
  if (aligning_ || !dragged_.empty())
    return;
  for (int i = 0; i < tabs_->count(); ++i) {
    auto* tab = tabs_->GetTabAtIndex(i);
    if (!tab->IsPinned() && !Eligible(tab))
      return;
  }
  base::AutoReset<bool> guard(&aligning_, true);
  int destination = tabs_->IndexOfFirstNonPinnedTab();
  for (const auto& id : tree_.PageOrder()) {
    auto* tab = TabFor(id);
    if (!tab)
      continue;
    const int index = tabs_->GetIndexOfTab(tab);
    if (index != destination)
      tabs_->MoveWebContentsAt(index, destination, false, std::nullopt);
    ++destination;
  }
}

void TabOrganizationController::BeginTabDrag() {
  dragged_ = SelectionFor();
  drop_parent_.reset();
  drop_before_.reset();
  if (dragged_.empty())
    return;
  auto selection = tabs_->selection_model().GetListSelectionModel();
  for (const auto& id : tree_.PagesIn(dragged_)) {
    if (auto* tab = TabFor(id))
      selection.AddIndexToSelection(tabs_->GetIndexOfTab(tab));
  }
  tabs_->SetSelectionFromModel(selection);
}

void TabOrganizationController::SetDropDestination(
    TreeId parent,
    std::optional<TreeId> before) {
  drop_parent_ = std::move(parent);
  drop_before_ = std::move(before);
}

void TabOrganizationController::EndTabDrag(bool completed) {
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(&TabOrganizationController::FinishTabDrag,
                                weak_factory_.GetWeakPtr(), completed));
}

void TabOrganizationController::FinishTabDrag(bool completed) {
  auto ids = std::exchange(dragged_, {});
  auto parent = std::exchange(drop_parent_, std::nullopt);
  auto before = std::exchange(drop_before_, std::nullopt);
  if (completed && parent) {
    if (ids.empty())
      ids = SelectionFor();  // A native drag from another window.
    const auto pages = tree_.PagesIn(ids);
    const bool remains_in_window = std::ranges::all_of(
        pages, [this](const TreeId& id) { return TabFor(id) != nullptr; });
    if (remains_in_window) {
      coordinator_.Invalidate(ids);
      tree_.Move(ids, *parent, before);
    }
  }
  // A cross-window move carries PageTreeState with each WebContents. Syncing
  // removes source entries; the destination restores its own metadata.
  Sync();
  AlignTabOrder();
  ScheduleUpdate();
}

void TabOrganizationController::OnTabStripModelChanged(
    TabStripModel*,
    const TabStripModelChange&,
    const TabStripSelectionChange&) {
  Sync();
}
void TabOrganizationController::OnTabChangedAt(tabs::TabInterface*,
                                               TabChangeType) {
  Sync();
}
void TabOrganizationController::OnTabPinnedStateChanged(tabs::TabInterface*,
                                                        int) {
  Sync();
}
void TabOrganizationController::TabGroupedStateChanged(
    TabStripModel*,
    std::optional<tab_groups::TabGroupId>,
    std::optional<tab_groups::TabGroupId>,
    tabs::TabInterface*,
    int) {
  Sync();
}
void TabOrganizationController::OnSplitTabChanged(const SplitTabChange&) {
  Sync();
}

void TabOrganizationController::ScheduleUpdate() {
  if (update_pending_)
    return;
  update_pending_ = true;
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(&TabOrganizationController::Update,
                                weak_factory_.GetWeakPtr()));
}

void TabOrganizationController::Update() {
  update_pending_ = false;
  auto* service =
      window_->GetProfile()->IsOffTheRecord()
          ? nullptr
          : SessionServiceFactory::GetForProfile(window_->GetProfile());
  for (const auto& [handle, id] : pages_) {
    auto* tab = handle.Get();
    if (!Eligible(tab))
      continue;
    auto* contents = tab->GetContents();
    if (!contents)
      continue;
    auto state = tree_.SavePage(id);
    auto transient = std::make_unique<PageTreeState>();
    transient->state = state.Clone();
    contents->SetUserData(&kPageTreeStateKey, std::move(transient));
    auto encoded = base::WriteJson(state);
    if (service && encoded) {
      service->AddTabExtraData(window_->GetSessionID(),
                               sessions::SessionTabHelper::IdForTab(contents),
                               kSessionKey, *encoded);
    }
  }
  changed_.Notify();
}

base::WeakPtr<TabOrganizationController>
TabOrganizationController::GetWeakPtr() {
  return weak_factory_.GetWeakPtr();
}

base::CallbackListSubscription TabOrganizationController::Observe(
    base::RepeatingClosure callback) {
  return changed_.Add(std::move(callback));
}

namespace {

constexpr base::TimeDelta kClassifyDebounce = base::Milliseconds(750);
constexpr base::TimeDelta kProviderTimeout = base::Seconds(10);

}  // namespace

bool TabOrganizationController::AutomaticEnabled() {
  Profile* profile = window_->GetProfile();
  if (!profile || profile->IsOffTheRecord()) {
    return false;
  }
  return CanOrganize(ReadAiConnectionConfig(profile->GetPrefs()));
}

void TabOrganizationController::TrackActivityAndSchedule() {
  for (auto it = activity_.begin(); it != activity_.end();) {
    if (!tree_.Find(it->first)) {
      snapshots_.erase(it->first);
      dispatched_activity_.erase(it->first);
      it = activity_.erase(it);
    } else {
      ++it;
    }
  }
  for (int i = 0; i < tabs_->count(); ++i) {
    auto* tab = tabs_->GetTabAtIndex(i);
    if (!Eligible(tab)) {
      continue;
    }
    auto* contents = tab->GetContents();
    if (!contents) {
      continue;
    }
    auto id = IdFor(tab);
    if (!id) {
      continue;
    }
    PageSnapshot snapshot{contents->GetTitle(),
                          contents->GetLastCommittedURL().spec()};
    auto existing = snapshots_.find(*id);
    if (existing == snapshots_.end() || existing->second.title != snapshot.title ||
        existing->second.url != snapshot.url) {
      snapshots_[*id] = std::move(snapshot);
      // A manual correction holds until activity changes; a new revision
      // re-arms automatic classification for this branch.
      ++activity_[*id];
    }
  }
  MaybeScheduleAutomatic();
}

void TabOrganizationController::MaybeScheduleAutomatic() {
  if (!AutomaticEnabled() || !dragged_.empty()) {
    if (!AutomaticEnabled()) {
      CancelAutomatic();
    }
    return;
  }
  bool due = false;
  for (const auto& [id, revision] : activity_) {
    const TabTree::Node* node = tree_.Find(id);
    if (!node || node->value.kind != TabTree::Kind::kPage) {
      continue;
    }
    auto dispatched = dispatched_activity_.find(id);
    if (dispatched == dispatched_activity_.end() ||
        dispatched->second < revision) {
      due = true;
      break;
    }
  }
  if (due && !classify_timer_.IsRunning()) {
    classify_timer_.Start(FROM_HERE, kClassifyDebounce,
                          base::BindOnce(&TabOrganizationController::OnClassifyTimer,
                                         base::Unretained(this)));
  }
}

std::string TabOrganizationController::BranchSourceDescription(
    const TreeId& page) const {
  const TabTree::Node* node = tree_.Find(page);
  const TabTree::Node* parent =
      node && node->parent() &&
              node->parent()->value.kind == TabTree::Kind::kPage
          ? node->parent()
          : nullptr;
  if (!parent) {
    return "no source tab";
  }
  return "opened from \"" +
         base::UTF16ToUTF8(parent->GetTitle()) + "\"";
}

std::vector<std::pair<std::string, std::string>>
TabOrganizationController::TaskOptions() const {
  std::vector<std::pair<std::string, std::string>> options;
  std::function<void(const TabTree::Node*)> visit =
      [&](const TabTree::Node* node) {
        if (node->value.kind == TabTree::Kind::kTask) {
          options.emplace_back(node->value.id.value(),
                               base::UTF16ToUTF8(node->GetTitle()));
        }
        for (const auto& child : node->children()) {
          visit(child.get());
        }
      };
  visit(tree_.root());
  options.emplace_back(kJevNewTaskOption, "Create a new task group");
  options.emplace_back(kJevScratchpadOption, "Leave ungrouped in Scratchpad");
  return options;
}

void TabOrganizationController::ClassifyNowForTesting() {
  classify_timer_.Stop();
  OnClassifyTimer();
}

void TabOrganizationController::OnClassifyTimer() {
  if (!AutomaticEnabled()) {
    return;
  }
  Profile* profile = window_->GetProfile();
  const AiConnectionConfig config =
      ReadAiConnectionConfig(profile->GetPrefs());
  ApiKeyStore keys;
  std::optional<std::string> jev_key =
      keys.GetKey(config.jev_key_account);
  if (!jev_key || jev_key->empty()) {
    return;
  }
  auto loader_factory = profile->GetDefaultStoragePartition()
                            ->GetURLLoaderFactoryForBrowserProcess();
  const std::vector<std::pair<std::string, std::string>> options =
      TaskOptions();
  // Group due pages by normalized branch root; the whole branch moves as one.
  std::map<TreeId, std::vector<TreeId>> branches;
  for (const auto& [id, revision] : activity_) {
    const TabTree::Node* node = tree_.Find(id);
    if (!node || node->value.kind != TabTree::Kind::kPage) {
      continue;
    }
    auto dispatched = dispatched_activity_.find(id);
    if (dispatched != dispatched_activity_.end() &&
        dispatched->second >= revision) {
      continue;
    }
    const std::vector<TreeId> roots = tree_.Normalize({id});
    if (roots.size() != 1) {
      continue;
    }
    branches[roots[0]].push_back(id);
  }
  for (const auto& [root, pages] : branches) {
    if (pages.empty() || !tree_.Find(root)) {
      continue;
    }
    // Classify from the first due page; the whole branch moves as one.
    const TreeId& representative = pages.front();
    auto snapshot = snapshots_.find(representative);
    if (snapshot == snapshots_.end()) {
      continue;
    }
    auto body = BuildJevRequestBody(
        base::UTF16ToUTF8(snapshot->second.title), snapshot->second.url,
        BranchSourceDescription(representative), options);
    if (!body) {
      continue;
    }
    auto activity = activity_.find(representative);
    const uint64_t request_id = coordinator_.RequestClassification(
        {root}, activity == activity_.end() ? 0 : activity->second);
    if (!request_id) {
      continue;
    }
    for (const TreeId& page : tree_.PagesIn({root})) {
      dispatched_activity_[page] = activity_[page];
    }
    auto client = std::make_unique<RemoteModelClient>(
        loader_factory, GURL(config.jev_endpoint), *jev_key, *body,
        kProviderTimeout);
    PendingClassification pending;
    pending.root = root;
    // Keep the loader alive via the pending map; completion is bound to this.
    pending.loader = std::move(client);
    pending_[request_id] = std::move(pending);
    pending_[request_id].loader->Send(
        base::BindOnce(&TabOrganizationController::OnJevResponse,
                       weak_factory_.GetWeakPtr(), request_id));
  }
  // Drop the secret from this scope as soon as dispatch finishes.
  jev_key->assign(jev_key->size(), '\0');
}

void TabOrganizationController::OnJevResponse(
    uint64_t request_id,
    RemoteModelClient::Outcome outcome) {
  pending_.erase(request_id);
  if (!outcome.ok || !AutomaticEnabled()) {
    return;
  }
  auto answer = ParseJevChoiceResponse(outcome.body, "destination");
  if (!answer) {
    return;
  }
  OrganizationCoordinator::Decision decision =
      OrganizationCoordinator::Decision::kLeave;
  TreeId destination{""};
  if (answer->choice == kJevScratchpadOption) {
    decision = OrganizationCoordinator::Decision::kLeave;
  } else if (answer->choice == kJevNewTaskOption) {
    decision = OrganizationCoordinator::Decision::kNewTask;
  } else {
    const TabTree::Node* node = tree_.Find(TreeId(answer->choice));
    if (!node || node->value.kind != TabTree::Kind::kTask) {
      return;
    }
    decision = OrganizationCoordinator::Decision::kMoveToTask;
    destination = node->value.id;
  }
  OrganizationCoordinator::Result result;
  result.request_id = request_id;
  result.decision = decision;
  result.destination_task = destination;
  result.confidence = answer->confidence;
  std::optional<TreeId> created = coordinator_.ApplyResult(result);
  if (created) {
    const std::vector<TreeId> pages = tree_.PagesIn({*created});
    notice_ = l10n_util::GetStringFUTF16(
        IDS_UW_AUTO_CREATED_TASK,
        {tree_.Find(*created) ? tree_.Find(*created)->GetTitle()
                              : std::u16string()});
    RequestTaskName(*created, pages);
  } else if (decision == OrganizationCoordinator::Decision::kMoveToTask) {
    const TabTree::Node* node = tree_.Find(destination);
    if (node) {
      notice_ = l10n_util::GetStringFUTF16(IDS_UW_AUTO_MOVED_TASK,
                                           {node->GetTitle()});
    }
  }
}

void TabOrganizationController::RequestTaskName(
    const TreeId& task,
    const std::vector<TreeId>& branch_pages) {
  Profile* profile = window_->GetProfile();
  const AiConnectionConfig config =
      ReadAiConnectionConfig(profile->GetPrefs());
  ApiKeyStore keys;
  std::optional<std::string> naming_key =
      keys.GetKey(config.naming_key_account);
  if (!CanOrganize(config) || !naming_key || naming_key->empty()) {
    return;
  }
  std::string prompt =
      "Name this browsing task as a short action-oriented phrase of at most "
      "five words. Tabs:\n";
  for (const TreeId& page : branch_pages) {
    auto snapshot = snapshots_.find(page);
    if (snapshot == snapshots_.end()) {
      continue;
    }
    prompt += "- " + base::UTF16ToUTF8(snapshot->second.title) + " (" +
              snapshot->second.url + ")\n";
  }
  auto body = BuildNamingRequestBody(config.naming_model, prompt);
  if (!body) {
    return;
  }
  const uint64_t naming_version = tree_.ManualVersion(task);
  auto client = std::make_unique<RemoteModelClient>(
      profile->GetDefaultStoragePartition()
          ->GetURLLoaderFactoryForBrowserProcess(),
      GURL(config.naming_endpoint), *naming_key, *body, kProviderTimeout);
  naming_key->assign(naming_key->size(), '\0');
  PendingNaming pending;
  pending.manual_version = naming_version;
  pending.loader = std::move(client);
  pending_naming_[task] = std::move(pending);
  pending_naming_[task].loader->Send(
      base::BindOnce(&TabOrganizationController::OnNamingResponse,
                     weak_factory_.GetWeakPtr(), task, naming_version));
}

void TabOrganizationController::OnNamingResponse(
    TreeId task,
    uint64_t naming_version,
    RemoteModelClient::Outcome outcome) {
  pending_naming_.erase(task);
  if (!outcome.ok) {
    return;
  }
  auto name = ParseNamingResponse(outcome.body);
  if (!name) {
    return;
  }
  if (coordinator_.ApplyNaming(task, *name, naming_version)) {
    notice_ = l10n_util::GetStringFUTF16(IDS_UW_AUTO_CREATED_TASK, {*name});
  }
}

void TabOrganizationController::CancelAutomatic() {
  classify_timer_.Stop();
  pending_.clear();
  pending_naming_.clear();
  jev_probe_.reset();
  naming_probe_.reset();
  coordinator_.InvalidateAll();
}

void TabOrganizationController::ValidateProviders() {
  Profile* profile = window_->GetProfile();
  if (!profile || profile->IsOffTheRecord()) {
    return;
  }
  AiConnectionConfig config = ReadAiConnectionConfig(profile->GetPrefs());
  // Require a complete configuration independent of the enabled flag.
  config.organization_enabled = true;
  if (!CanOrganize(config)) {
    return;
  }
  ApiKeyStore keys;
  std::optional<std::string> jev_key = keys.GetKey(config.jev_key_account);
  std::optional<std::string> naming_key =
      keys.GetKey(config.naming_key_account);
  if (!jev_key || jev_key->empty() || !naming_key || naming_key->empty()) {
    return;
  }
  auto jev_body = BuildJevRequestBody(
      "Connectivity check", "about:blank", "setup validation",
      {{kJevScratchpadOption, "Leave ungrouped in Scratchpad"}});
  auto naming_body = BuildNamingRequestBody(config.naming_model,
                                            "Reply with the single word: ok");
  if (!jev_body || !naming_body) {
    return;
  }
  auto loader_factory = profile->GetDefaultStoragePartition()
                            ->GetURLLoaderFactoryForBrowserProcess();
  jev_probe_ = std::make_unique<RemoteModelClient>(
      loader_factory, GURL(config.jev_endpoint), *jev_key, *jev_body,
      base::Seconds(15));
  naming_probe_ = std::make_unique<RemoteModelClient>(
      loader_factory, GURL(config.naming_endpoint), *naming_key,
      *naming_body, base::Seconds(15));
  jev_probe_ok_ = false;
  naming_probe_ok_ = false;
  jev_probe_->Send(base::BindOnce(
      &TabOrganizationController::OnValidationResponse,
      weak_factory_.GetWeakPtr(), /*is_jev=*/true));
  naming_probe_->Send(base::BindOnce(
      &TabOrganizationController::OnValidationResponse,
      weak_factory_.GetWeakPtr(), /*is_jev=*/false));
}

void TabOrganizationController::OnValidationResponse(
    bool is_jev,
    RemoteModelClient::Outcome outcome) {
  if (is_jev) {
    jev_probe_.reset();
    jev_probe_ok_ =
        outcome.ok &&
        ParseJevChoiceResponse(outcome.body, "destination").has_value();
  } else {
    naming_probe_.reset();
    naming_probe_ok_ =
        outcome.ok && ParseNamingResponse(outcome.body).has_value();
  }
  if (jev_probe_ || naming_probe_) {
    return;
  }
  Profile* profile = window_->GetProfile();
  if (!profile || profile->IsOffTheRecord()) {
    return;
  }
  if (jev_probe_ok_ && naming_probe_ok_) {
    profile->GetPrefs()->SetBoolean(kOrganizationEnabledPref, true);
    notice_ = l10n_util::GetStringUTF16(IDS_UW_AUTO_ENABLED);
  } else {
    notice_ = l10n_util::GetStringUTF16(IDS_UW_AUTO_SETUP_ERROR);
  }
}

}  // namespace uw
