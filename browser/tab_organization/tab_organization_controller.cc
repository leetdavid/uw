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
#include "base/supports_user_data.h"
#include "base/task/sequenced_task_runner.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/sessions/session_service.h"
#include "chrome/browser/sessions/session_service_factory.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/grit/generated_resources.h"
#include "components/sessions/content/session_tab_helper.h"
#include "content/public/browser/web_contents.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/menus/simple_menu_model.h"

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
      user_data_(window->GetUnownedUserDataHost(), *this) {
  tree_subscription_ = tree_.Observe(base::BindRepeating(
      &TabOrganizationController::ScheduleUpdate, weak_factory_.GetWeakPtr()));
  tabs_->AddObserver(this);
  Sync();
}

TabOrganizationController::~TabOrganizationController() {
  tabs_->RemoveObserver(this);
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
  for (auto it = pages_.begin(); it != pages_.end();) {
    if (live.contains(it->first)) {
      ++it;
      continue;
    }
    tree_.RemovePage(it->second);
    it = pages_.erase(it);
  }
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
  }
  tree_.ResolveRestoredParents();
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
  auto result = tree_.MakeTask(ids, std::move(title));
  AlignTabOrder();
  return result;
}

bool TabOrganizationController::Move(const std::vector<TreeId>& ids,
                                     const TreeId& parent,
                                     std::optional<TreeId> before) {
  if (!tree_.Move(ids, parent, before))
    return false;
  AlignTabOrder();
  return true;
}

void TabOrganizationController::Rename(const TreeId& id, std::u16string title) {
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
  tree_.Undo();
  AlignTabOrder();
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
    if (remains_in_window)
      tree_.Move(ids, *parent, before);
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

}  // namespace uw
