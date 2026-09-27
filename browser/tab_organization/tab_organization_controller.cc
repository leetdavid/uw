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
#include "components/content_extraction/content/browser/inner_text.h"
#include "components/prefs/pref_service.h"
#include "components/sessions/content/session_tab_helper.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/render_frame_host.h"
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
  for (const char* pref :
       {kOrganizationModePref, kOrganizationEndpointPref,
        kOrganizationModelPref, kOrganizationKeyAccountPref,
        kOrganizationEnabledPref, kOrganizationPauseModePref,
        kOrganizationPauseUntilPref}) {
    pref_registrar_.Add(pref,
                        base::BindRepeating(&TabOrganizationController::
 OnConfigChanged,
                                            base::Unretained(this)));
  }
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

std::optional<TabOrganizationController::Notice>
TabOrganizationController::TakeNotice() {
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
constexpr base::TimeDelta kValidationTimeout = base::Seconds(15);

}  // namespace

bool TabOrganizationController::AutomaticEnabled() {
  Profile* profile = window_->GetProfile();
  if (!profile || profile->IsOffTheRecord()) {
    return false;
  }
  return CanOrganize(ReadAiConnectionConfig(profile->GetPrefs())) &&
         ReadPauseState(profile->GetPrefs()) == PauseMode::kOff;
}

void TabOrganizationController::SetPause(PauseMode mode,
                                         base::TimeDelta duration) {
  SetPauseState(window_->GetProfile()->GetPrefs(), mode, duration);
  OnConfigChanged();
}

PauseMode TabOrganizationController::pause_mode() const {
  return ReadPauseState(window_->GetProfile()->GetPrefs());
}

void TabOrganizationController::TrackActivityAndSchedule() {
  for (auto it = activity_.begin(); it != activity_.end();) {
    if (!tree_.Find(it->first)) {
      snapshots_.erase(it->first);
      excerpts_.erase(it->first);
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
      // re-arms automatic classification for this branch and turns older
      // outstanding responses into no-ops.
      ++activity_[*id];
      coordinator_.Invalidate({*id});
      RequestExcerpt(tab, *id, activity_[*id]);
    }
  }
  MaybeScheduleAutomatic();
}

void TabOrganizationController::MaybeScheduleAutomatic() {
  if (!AutomaticEnabled()) {
    CancelAutomatic();
    return;
  }
  if (!dragged_.empty()) {
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

std::string TabOrganizationController::ManualPlacementDescription(
    const TreeId& page) const {
  bool manual = false;
  for (const TabTree::Node* node = tree_.Find(page); node;
       node = node->parent()) {
    if (tree_.ManualVersion(node->value.id) != 0) {
      manual = true;
      break;
    }
  }
  if (!manual) {
    return std::string();
  }
  const TabTree::Node* task = tree_.TaskFor(page);
  if (!task) {
    return "the user left this branch ungrouped";
  }
  return "the user manually placed this branch in \"" +
         base::UTF16ToUTF8(task->GetTitle()) + "\"";
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
  return options;
}

void TabOrganizationController::RequestExcerpt(tabs::TabInterface* tab,
                                               const TreeId& id,
                                               uint64_t revision) {
  if (!AutomaticEnabled()) {
    return;
  }
  auto* contents = tab ? tab->GetContents() : nullptr;
  content::RenderFrameHost* frame =
      contents ? contents->GetPrimaryMainFrame() : nullptr;
  if (!frame) {
    return;
  }
  content_extraction::GetInnerText(
      *frame, std::nullopt,
      base::BindOnce(
          [](base::WeakPtr<TabOrganizationController> self, TreeId id,
             uint64_t revision,
             std::unique_ptr<content_extraction::InnerTextResult> result) {
            if (self && result) {
              self->StoreExcerpt(id, revision, std::move(result->inner_text));
            }
          },
          weak_factory_.GetWeakPtr(), id, revision));
}

void TabOrganizationController::StoreExcerpt(TreeId id,
                                             uint64_t revision,
                                             std::string text) {
  excerpts_[id] = {std::move(text), revision};
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
  std::string api_key;
  if (config.mode == ConnectionMode::kOpenAiCompatible) {
    ApiKeyStore keys;
    std::optional<std::string> key = keys.GetKey(config.key_account);
    if (!key || key->empty()) {
      return;
    }
    api_key = std::move(*key);
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
    BranchContext context;
    context.title = base::UTF16ToUTF8(snapshot->second.title);
    context.url = snapshot->second.url;
    context.source_description = BranchSourceDescription(representative);
    context.manual_placement = ManualPlacementDescription(representative);
    auto activity = activity_.find(representative);
    const uint64_t revision =
        activity == activity_.end() ? 0 : activity->second;
    if (auto excerpt = excerpts_.find(representative);
        excerpt != excerpts_.end() && excerpt->second.revision == revision) {
      std::u16string text = base::UTF8ToUTF16(excerpt->second.text);
      if (text.size() > kMaxExcerptLength) {
        text.resize(kMaxExcerptLength);
      }
      context.excerpt = base::UTF16ToUTF8(text);
    }
    auto body =
        BuildClassificationRequestBody(config.model, context, options);
    if (!body) {
      continue;
    }
    const uint64_t request_id = coordinator_.RequestClassification({root});
    if (!request_id) {
      continue;
    }
    for (const TreeId& page : tree_.PagesIn({root})) {
      dispatched_activity_[page] = activity_[page];
    }
    auto client = std::make_unique<RemoteModelClient>(
        loader_factory, ChatCompletionsUrl(config.endpoint), api_key, *body,
        kProviderTimeout);
    pending_[request_id] = std::move(client);
    pending_[request_id]->Send(
        base::BindOnce(&TabOrganizationController::OnClassificationResponse,
                       weak_factory_.GetWeakPtr(), request_id));
  }
  // Drop the secret from this scope as soon as dispatch finishes.
  api_key.assign(api_key.size(), '\0');
}

void TabOrganizationController::OnClassificationResponse(
    uint64_t request_id,
    RemoteModelClient::Outcome outcome) {
  pending_.erase(request_id);
  if (!outcome.ok || !AutomaticEnabled()) {
    return;
  }
  auto answer = ParseClassificationResponse(outcome.body);
  if (!answer) {
    return;
  }
  OrganizationCoordinator::Result result;
  result.request_id = request_id;
  result.confidence = answer->confidence;
  if (answer->task_name) {
    result.task_name = *answer->task_name;
  }
  if (answer->choice == kScratchpadOption) {
    result.decision = OrganizationCoordinator::Decision::kLeave;
  } else if (answer->choice == kNewTaskOption) {
    result.decision = OrganizationCoordinator::Decision::kNewTask;
  } else {
    const TabTree::Node* node = tree_.Find(TreeId(answer->choice));
    if (!node || node->value.kind != TabTree::Kind::kTask) {
      return;
    }
    result.decision = OrganizationCoordinator::Decision::kMoveToTask;
    result.destination_task = node->value.id;
  }
  std::optional<TreeId> affected = coordinator_.ApplyResult(result);
  if (!affected) {
    return;
  }
  const TabTree::Node* node = tree_.Find(*affected);
  notice_ =
      Notice{l10n_util::GetStringFUTF16(
                 result.decision == OrganizationCoordinator::Decision::kNewTask
                     ? IDS_UW_AUTO_CREATED_TASK
                     : IDS_UW_AUTO_MOVED_TASK,
                 {node ? node->GetTitle() : std::u16string()}),
             /*undoable=*/true};
}

void TabOrganizationController::CancelAutomatic() {
  classify_timer_.Stop();
  pending_.clear();
  probe_.reset();
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
  std::string api_key;
  if (config.mode == ConnectionMode::kOpenAiCompatible) {
    ApiKeyStore keys;
    std::optional<std::string> key = keys.GetKey(config.key_account);
    if (!key || key->empty()) {
      notice_ = Notice{l10n_util::GetStringUTF16(IDS_UW_AUTO_SETUP_ERROR),
                       /*undoable=*/false};
      ScheduleUpdate();
      return;
    }
    api_key = std::move(*key);
  }
  BranchContext context;
  context.title = "Connectivity check";
  context.url = "about:blank";
  context.source_description = "setup validation";
  auto body = BuildClassificationRequestBody(config.model, context, {});
  if (!body) {
    return;
  }
  probe_ = std::make_unique<RemoteModelClient>(
      profile->GetDefaultStoragePartition()
          ->GetURLLoaderFactoryForBrowserProcess(),
      ChatCompletionsUrl(config.endpoint), std::move(api_key), *body,
      kValidationTimeout);
  probe_->Send(
      base::BindOnce(&TabOrganizationController::OnValidationResponse,
                     weak_factory_.GetWeakPtr()));
}

void TabOrganizationController::OnValidationResponse(
    RemoteModelClient::Outcome outcome) {
  probe_.reset();
  Profile* profile = window_->GetProfile();
  if (!profile || profile->IsOffTheRecord()) {
    return;
  }
  if (outcome.ok && ParseClassificationResponse(outcome.body)) {
    profile->GetPrefs()->SetBoolean(kOrganizationEnabledPref, true);
    notice_ = Notice{l10n_util::GetStringUTF16(IDS_UW_AUTO_ENABLED),
                     /*undoable=*/false};
  } else {
    notice_ = Notice{l10n_util::GetStringUTF16(IDS_UW_AUTO_SETUP_ERROR),
                     /*undoable=*/false};
  }
  ScheduleUpdate();
}

}  // namespace uw
