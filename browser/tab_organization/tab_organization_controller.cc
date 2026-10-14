// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/browser/tab_organization/tab_organization_controller.h"

#include <algorithm>
#include <utility>

#include "base/auto_reset.h"
#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/sequenced_task_runner.h"
#include "base/token.h"
#include "base/uuid.h"
#include "base/values.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/sessions/session_service.h"
#include "chrome/browser/sessions/session_service_factory.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/tabs/tab_enums.h"
#include "chrome/browser/ui/tabs/tab_group_model.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/common/pref_names.h"
#include "chrome/grit/generated_resources.h"
#include "components/prefs/pref_service.h"
#include "components/sessions/content/session_tab_helper.h"
#include "components/tabs/public/tab_group.h"
#include "content/public/browser/web_contents.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/menus/simple_menu_model.h"

namespace uw {
namespace {

constexpr size_t kMaxUndoActions = 100;

std::string TaskOf(const tabs::TabInterface* tab) {
  const std::optional<tab_groups::TabGroupId> group = tab->GetGroup();
  return group ? group->ToString() : std::string();
}

bool Has(const std::vector<PageId>& ids, const PageId& id) {
  return std::ranges::find(ids, id) != ids.end();
}

}  // namespace

DEFINE_USER_DATA(TabOrganizationController);

TabOrganizationController::TabOrganizationController(
    BrowserWindowInterface* window)
    : window_(window),
      tabs_(window->GetTabStripModel()),
      user_data_(window->GetUnownedUserDataHost(), *this) {
  tabs_->AddObserver(this);
  Mirror({});
  active_ = IdFor(tabs_->GetActiveTab());
  Remember(active_);
}

TabOrganizationController::~TabOrganizationController() {
  tabs_->RemoveObserver(this);
}

bool TabOrganizationController::IsCommand(int command) {
  return command >= kNewTask && command <= kLastCommand;
}

void TabOrganizationController::AppendCommands(ui::SimpleMenuModel* menu) {
  menu->AddItem(kNewTask, l10n_util::GetStringUTF16(IDS_UW_NEW_TASK));
  menu->AddItem(kScratchpad,
                l10n_util::GetStringUTF16(IDS_UW_MOVE_TO_SCRATCHPAD));
  menu->AddItem(kNest, l10n_util::GetStringUTF16(IDS_UW_NEST_BRANCH));
  menu->AddItem(kOutdent, l10n_util::GetStringUTF16(IDS_UW_OUTDENT_BRANCH));
  menu->AddItem(kUndo, l10n_util::GetStringUTF16(IDS_UW_UNDO_ORGANIZATION));
  menu->AddSeparator(ui::NORMAL_SEPARATOR);
  menu->AddItem(kCloseBranch, l10n_util::GetStringUTF16(IDS_UW_CLOSE_BRANCH));
}

TabOrganizationController* TabOrganizationController::From(
    BrowserWindowInterface* window) {
  return window ? Get(window->GetUnownedUserDataHost()) : nullptr;
}

TabOrganizationController* TabOrganizationController::From(
    TabStripModel* model) {
  tabs::TabInterface* tab = model ? model->GetActiveTab() : nullptr;
  return tab ? From(tab->GetBrowserWindowInterface()) : nullptr;
}

bool TabOrganizationController::OpenNewTask(BrowserWindowInterface* window) {
  TabOrganizationController* self = From(window);
  if (!self) {
    return false;
  }
  chrome::NewTab(window, NewTabTypes::kNewTabCommand);
  if (const std::optional<PageId> page =
          self->IdFor(self->tabs_->GetActiveTab())) {
    self->MoveToNewTask({*page},
                        l10n_util::GetStringUTF16(IDS_UW_UNTITLED_TASK));
  }
  return true;
}

void TabOrganizationController::DidOpenTab(BrowserWindowInterface* window,
                                           content::WebContents* page,
                                           content::WebContents* source) {
  TabOrganizationController* self = From(window);
  if (!self || !page || page == source) {
    return;
  }
  self->Mirror({});
  const std::optional<PageId> id =
      self->IdFor(self->tabs_->GetTabForWebContents(page));
  if (!id) {
    return;
  }
  self->opened_.insert(*id);
  if (source) {
    if (const std::optional<PageId> parent =
            self->IdFor(self->tabs_->GetTabForWebContents(source))) {
      self->tree_.SetParent(*id, *parent);
    }
  } else if (const std::optional<PageId>& beside =
                 self->active_ == id ? self->previous_active_ : self->active_) {
    self->tree_.OpenBeside(*id, *beside);
  }
  self->ScheduleApply();
  self->ScheduleUpdate();
}

void TabOrganizationController::RestoreTab(
    BrowserWindowInterface* window,
    content::WebContents* page,
    const std::map<std::string, std::string>& extra_data) {
  TabOrganizationController* self = From(window);
  const auto saved = extra_data.find(kSessionKey);
  if (!self || !page || saved == extra_data.end()) {
    return;
  }
  const std::optional<base::DictValue> state =
      base::JSONReader::ReadDict(saved->second, base::JSON_PARSE_RFC);
  tabs::TabInterface* tab = self->tabs_->GetTabForWebContents(page);
  const std::string* id = state ? state->FindString("id") : nullptr;
  if (!tab || tab->IsPinned() || !id ||
      !base::Uuid::ParseLowercase(*id).is_valid() ||
      self->TabFor(PageId(*id))) {
    return;
  }
  self->ids_[tab->GetHandle()] = PageId(*id);
  const std::string* parent = state->FindString("parent");
  if (parent && !parent->empty()) {
    self->restored_parents_[PageId(*id)] = PageId(*parent);
  }
  const std::string* task_name = state->FindString("task_name");
  if (task_name && tab->GetGroup()) {
    self->generated_names_.try_emplace(TaskOf(tab),
                                       base::UTF8ToUTF16(*task_name));
  }
  self->Mirror({});
  self->ScheduleApply();
}

std::optional<PageId> TabOrganizationController::IdFor(
    const tabs::TabInterface* tab) const {
  if (!tab) {
    return std::nullopt;
  }
  const auto it = ids_.find(tab->GetHandle());
  return it == ids_.end() ? std::nullopt : std::make_optional(it->second);
}

tabs::TabInterface* TabOrganizationController::TabFor(const PageId& id) const {
  for (const auto& [handle, page] : ids_) {
    if (page == id) {
      return handle.Get();
    }
  }
  return nullptr;
}

int TabOrganizationController::DepthFor(const tabs::TabInterface* tab) const {
  const std::optional<PageId> id = IdFor(tab);
  return id ? tree_.DepthOf(*id) : 0;
}

std::vector<PageId> TabOrganizationController::SelectionFor(
    const tabs::TabInterface* context) const {
  std::vector<PageId> selection;
  const int context_index =
      context ? tabs_->GetIndexOfTab(context) : TabStripModel::kNoTab;
  if (context && context_index == TabStripModel::kNoTab) {
    return selection;
  }
  if (context && !tabs_->IsTabSelected(context_index)) {
    if (const std::optional<PageId> id = IdFor(context)) {
      selection.push_back(*id);
    }
    return selection;
  }
  for (int index :
       tabs_->selection_model().GetListSelectionModel().selected_indices()) {
    if (const std::optional<PageId> id = IdFor(tabs_->GetTabAtIndex(index))) {
      selection.push_back(*id);
    }
  }
  return selection;
}

bool TabOrganizationController::CanExecute(
    int command,
    const std::vector<PageId>& ids) const {
  if (command == kUndo) {
    return CanUndo();
  }
  const std::vector<PageId> roots = Roots(ids);
  if (roots.empty()) {
    return false;
  }
  switch (command) {
    case kNewTask:
      return tabs_->SupportsTabGroups();
    case kScratchpad:
      return std::ranges::any_of(roots, [this](const PageId& id) {
        const Page* page = tree_.Find(id);
        return !page->task.empty() || !page->parent.value().empty();
      });
    case kNest:
      return roots.size() == 1 && tree_.PreviousSibling(roots.front());
    case kOutdent:
      return roots.size() == 1 &&
             !tree_.Find(roots.front())->parent.value().empty();
    case kMoveUp:
      return roots.size() == 1 && tree_.PreviousSibling(roots.front());
    case kMoveDown:
      return roots.size() == 1 && tree_.NextSibling(roots.front());
    case kCloseBranch:
      return std::ranges::any_of(roots, [this](const PageId& id) {
        return tree_.BranchOf(id).size() > 1;
      });
    default:
      return false;
  }
}

void TabOrganizationController::Execute(int command,
                                        const std::vector<PageId>& ids) {
  if (!CanExecute(command, ids)) {
    return;
  }
  switch (command) {
    case kNewTask:
      MoveToNewTask(ids, l10n_util::GetStringUTF16(IDS_UW_UNTITLED_TASK));
      break;
    case kScratchpad:
      MoveToScratchpad(ids);
      break;
    case kNest:
      Nest(Roots(ids).front());
      break;
    case kOutdent:
      Outdent(Roots(ids).front());
      break;
    case kMoveUp:
      MoveUp(Roots(ids).front());
      break;
    case kMoveDown:
      MoveDown(Roots(ids).front());
      break;
    case kCloseBranch:
      CloseBranches(ids);
      break;
    case kUndo:
      Undo();
      break;
  }
}

std::optional<tab_groups::TabGroupId> TabOrganizationController::MoveToNewTask(
    const std::vector<PageId>& ids,
    std::u16string title,
    Origin origin,
    const std::vector<PageId>& staying) {
  const std::vector<PageId> roots = Roots(ids);
  if (roots.empty() || !tabs_->SupportsTabGroups()) {
    return std::nullopt;
  }
  const std::string task = tab_groups::TabGroupId::GenerateNew().ToString();
  Record();
  LeaveBehind(roots, staying);
  tree_.MoveToNewTask(roots.front(), task);
  for (size_t i = 1; i < roots.size(); ++i) {
    tree_.MoveToTask(roots[i], task);
  }
  if (origin == Origin::kOrganizer) {
    generated_names_[task] = title;
  }
  pending_tasks_[task] = {std::move(title), std::nullopt};
  Apply();
  if (origin == Origin::kUser) {
    NotifyManualMove(roots);
  }
  return GroupFor(task);
}

bool TabOrganizationController::MoveToTask(const std::vector<PageId>& ids,
                                           const tab_groups::TabGroupId& group,
                                           Origin origin,
                                           const std::vector<PageId>& staying) {
  const std::vector<PageId> roots = Roots(ids);
  const std::string task = group.ToString();
  if (roots.empty() || !GroupFor(task)) {
    return false;
  }
  Record();
  LeaveBehind(roots, staying);
  for (const PageId& root : roots) {
    tree_.MoveToTask(root, task);
  }
  Apply();
  if (origin == Origin::kUser) {
    NotifyManualMove(roots);
  }
  return true;
}

bool TabOrganizationController::MoveToScratchpad(
    const std::vector<PageId>& ids,
    Origin origin,
    const std::vector<PageId>& staying) {
  const std::vector<PageId> roots = Roots(ids);
  if (roots.empty()) {
    return false;
  }
  Record();
  LeaveBehind(roots, staying);
  for (const PageId& root : roots) {
    tree_.MoveToTask(root, std::string());
  }
  Apply();
  if (origin == Origin::kUser) {
    NotifyManualMove(roots);
  }
  return true;
}

bool TabOrganizationController::Nest(const PageId& id) {
  Record();
  if (!tree_.Nest(id)) {
    undo_.pop_back();
    return false;
  }
  Apply();
  NotifyManualMove({id});
  return true;
}

bool TabOrganizationController::Outdent(const PageId& id) {
  Record();
  if (!tree_.Outdent(id)) {
    undo_.pop_back();
    return false;
  }
  Apply();
  NotifyManualMove({id});
  return true;
}

bool TabOrganizationController::MoveUp(const PageId& id) {
  Record();
  if (!tree_.MoveUp(id)) {
    undo_.pop_back();
    return false;
  }
  Apply();
  NotifyManualMove({id});
  return true;
}

bool TabOrganizationController::MoveDown(const PageId& id) {
  Record();
  if (!tree_.MoveDown(id)) {
    undo_.pop_back();
    return false;
  }
  Apply();
  NotifyManualMove({id});
  return true;
}

void TabOrganizationController::CloseBranches(const std::vector<PageId>& ids) {
  std::vector<PageId> closing;
  for (const PageId& root : Roots(ids)) {
    const std::vector<PageId> branch = tree_.BranchOf(root);
    closing.insert(closing.end(), branch.begin(), branch.end());
  }
  // Closing the last tab can destroy this controller, and beforeunload can
  // delay the close, so the tabs are looked up again when it runs.
  tabs_->ExecuteCloseTabsCommand(
      base::BindRepeating(
          [](base::WeakPtr<TabOrganizationController> self,
             const std::vector<PageId>& pages) {
            std::vector<tabs::TabInterface*> tabs;
            for (const PageId& page : pages) {
              tabs::TabInterface* tab = self ? self->TabFor(page) : nullptr;
              if (tab) {
                tabs.push_back(tab);
              }
            }
            return tabs;
          },
          weak_factory_.GetWeakPtr(), closing),
      false);
}

bool TabOrganizationController::SelectTask(const std::string& task) {
  const auto in_task = [&](const PageId& id) {
    const Page* page = tree_.Find(id);
    return page && page->task == task;
  };
  auto page = std::ranges::find_if(recent_, in_task);
  std::optional<PageId> target;
  if (page != recent_.end()) {
    target = *page;
  } else if (auto first = std::ranges::find(tree_.pages(), task, &Page::task);
             first != tree_.pages().end()) {
    target = first->id;
  }
  tabs::TabInterface* tab = target ? TabFor(*target) : nullptr;
  if (!tab) {
    return false;
  }
  tabs_->ActivateTabAt(tabs_->GetIndexOfTab(tab));
  return true;
}

void TabOrganizationController::Remember(const std::optional<PageId>& id) {
  std::erase_if(recent_, [&](const PageId& page) {
    return page == id || !tree_.Find(page);
  });
  if (id) {
    recent_.insert(recent_.begin(), *id);
  }
}

void TabOrganizationController::Undo() {
  if (undo_.empty()) {
    return;
  }
  Snapshot snapshot = std::move(undo_.back());
  undo_.pop_back();
  const PageTree before = tree_;
  tree_.Restore(snapshot.tree);
  for (const std::string& task : tree_.Tasks()) {
    const auto visuals = snapshot.tasks.find(task);
    if (!GroupFor(task) && visuals != snapshot.tasks.end()) {
      pending_tasks_[task] = {std::u16string(), visuals->second};
    }
  }
  Apply();
  // A restored placement counts as the user's own.
  std::vector<PageId> restored;
  for (const Page& page : tree_.pages()) {
    const Page* earlier = before.Find(page.id);
    if (earlier &&
        (earlier->task != page.task || earlier->parent != page.parent)) {
      restored.push_back(page.id);
    }
  }
  if (!restored.empty()) {
    manual_moves_.Notify(restored);
  }
}

void TabOrganizationController::BeginTabDrag() {
  dragging_ = true;
}

void TabOrganizationController::EndTabDrag() {
  dragging_ = false;
  ScheduleApply();
}

std::u16string TabOrganizationController::TaskTitle(
    const std::string& task) const {
  const std::optional<tab_groups::TabGroupId> group = GroupFor(task);
  return group
             ? tabs_->group_model()->GetTabGroup(*group)->visual_data()->title()
             : std::u16string();
}

void TabOrganizationController::NameTask(const std::string& task,
                                         const std::u16string& title) {
  const std::optional<tab_groups::TabGroupId> group = GroupFor(task);
  if (!group) {
    return;
  }
  const tab_groups::TabGroupVisualData* visuals =
      tabs_->group_model()->GetTabGroup(*group)->visual_data();
  generated_names_[task] = title;
  tabs_->ChangeTabGroupVisuals(
      *group, tab_groups::TabGroupVisualData(title, visuals->color(),
                                             visuals->is_collapsed()));
  ScheduleUpdate();
}

bool TabOrganizationController::HasGeneratedName(
    const std::string& task) const {
  const auto generated = generated_names_.find(task);
  return generated != generated_names_.end() &&
         generated->second == TaskTitle(task);
}

bool TabOrganizationController::HasManualName(const std::string& task) const {
  const std::u16string title = TaskTitle(task);
  return !title.empty() &&
         title != l10n_util::GetStringUTF16(IDS_UW_UNTITLED_TASK) &&
         !HasGeneratedName(task);
}

void TabOrganizationController::NameAutomatically(const std::string& task) {
  if (!HasManualName(task)) {
    return;
  }
  generated_names_[task] = TaskTitle(task);
  ScheduleUpdate();
}

base::CallbackListSubscription TabOrganizationController::Observe(
    base::RepeatingClosure callback) {
  return changed_.Add(std::move(callback));
}

base::CallbackListSubscription TabOrganizationController::ObserveManualMoves(
    ManualMoveCallback callback) {
  return manual_moves_.Add(std::move(callback));
}

void TabOrganizationController::OnTabStripModelChanged(
    TabStripModel*,
    const TabStripModelChange& change,
    const TabStripSelectionChange& selection) {
  if (applying_) {
    return;
  }
  std::set<PageId> moved;
  if (change.type() == TabStripModelChange::kMoved) {
    if (const std::optional<PageId> id = IdFor(change.GetMove()->tab)) {
      moved.insert(*id);
    }
  }
  Mirror(moved);
  NotifyNativeMoves(moved);
  if (selection.active_tab_changed()) {
    if (const std::optional<PageId> old = IdFor(selection.old_tab)) {
      previous_active_ = old;
    }
    active_ = IdFor(selection.new_tab);
    Remember(active_);
  }
  ScheduleApply();
}

void TabOrganizationController::OnTabPinnedStateChanged(tabs::TabInterface*,
                                                        int) {
  if (applying_) {
    return;
  }
  Mirror({});
  ScheduleApply();
}

void TabOrganizationController::OnTabGroupChanged(
    const TabGroupChange& change) {
  if (change.type == TabGroupChange::kVisualsChanged) {
    ScheduleUpdate();
  }
}

void TabOrganizationController::TabGroupedStateChanged(
    TabStripModel*,
    std::optional<tab_groups::TabGroupId>,
    std::optional<tab_groups::TabGroupId>,
    tabs::TabInterface* tab,
    int) {
  if (applying_) {
    return;
  }
  std::set<PageId> moved;
  if (const std::optional<PageId> id = IdFor(tab)) {
    moved.insert(*id);
  }
  Mirror(moved);
  NotifyNativeMoves(moved);
  ScheduleApply();
}

void TabOrganizationController::OnSplitTabChanged(const SplitTabChange&) {
  if (applying_) {
    return;
  }
  Mirror({});
  ScheduleApply();
}

void TabOrganizationController::NotifyNativeMoves(
    const std::set<PageId>& moved) {
  std::vector<PageId> manual;
  for (const PageId& id : moved) {
    if (!fresh_.contains(id)) {
      manual.push_back(id);
    }
  }
  if (!manual.empty()) {
    manual_moves_.Notify(manual);
  }
}

void TabOrganizationController::NotifyManualMove(
    const std::vector<PageId>& roots) {
  std::vector<PageId> moved;
  for (const PageId& root : roots) {
    const std::vector<PageId> branch = tree_.BranchOf(root);
    moved.insert(moved.end(), branch.begin(), branch.end());
  }
  manual_moves_.Notify(moved);
}

std::vector<PageId> TabOrganizationController::Roots(
    const std::vector<PageId>& ids) const {
  std::vector<PageId> roots;
  for (const Page& page : tree_.pages()) {
    if (!Has(ids, page.id)) {
      continue;
    }
    bool covered = false;
    for (const Page* up = tree_.Find(page.parent); up && !covered;
         up = tree_.Find(up->parent)) {
      covered = Has(ids, up->id);
    }
    if (!covered) {
      roots.push_back(page.id);
    }
  }
  return roots;
}

void TabOrganizationController::LeaveBehind(
    const std::vector<PageId>& roots,
    const std::vector<PageId>& staying) {
  const auto inside = [&](const PageId& id) {
    return std::ranges::any_of(roots, [&](const PageId& root) {
      return root != id && Has(tree_.BranchOf(root), id);
    });
  };
  for (const PageId& id : staying) {
    while (inside(id) && tree_.Outdent(id)) {
    }
  }
}

std::optional<tab_groups::TabGroupId> TabOrganizationController::GroupFor(
    const std::string& task) const {
  if (!tabs_->SupportsTabGroups()) {
    return std::nullopt;
  }
  for (const tab_groups::TabGroupId& group :
       tabs_->group_model()->ListTabGroups()) {
    if (group.ToString() == task) {
      return group;
    }
  }
  return std::nullopt;
}

void TabOrganizationController::Mirror(const std::set<PageId>& moved) {
  std::set<tabs::TabHandle> live;
  std::vector<Page> strip;
  for (int i = 0; i < tabs_->count(); ++i) {
    const tabs::TabInterface* tab = tabs_->GetTabAtIndex(i);
    if (tab->IsPinned()) {
      continue;
    }
    live.insert(tab->GetHandle());
    const auto [it, added] = ids_.try_emplace(tab->GetHandle());
    if (added) {
      it->second = PageId(base::Uuid::GenerateRandomV4().AsLowercaseString());
      fresh_.insert(it->second);
    }
    strip.push_back({it->second, TaskOf(tab), PageId()});
  }
  std::erase_if(
      ids_, [&live](const auto& entry) { return !live.contains(entry.first); });
  tree_.Adopt(strip, moved);
  std::erase_if(opened_, [this](const PageId& id) { return !tree_.Find(id); });
  LinkRestoredPages();
  ScheduleUpdate();
}

void TabOrganizationController::LinkRestoredPages() {
  for (auto it = restored_parents_.begin(); it != restored_parents_.end();) {
    const Page* child = tree_.Find(it->first);
    const Page* parent = tree_.Find(it->second);
    if (child && !parent) {
      ++it;
      continue;
    }
    if (child && child->task == parent->task) {
      tree_.SetParent(it->first, it->second);
    }
    it = restored_parents_.erase(it);
  }
}

void TabOrganizationController::Apply() {
  if (applying_ || !tabs_->SupportsTabGroups()) {
    return;
  }
  {
    base::AutoReset<bool> guard(&applying_, true);
    ApplyTasks();
    ApplyOrder();
  }
  Mirror({});
}

void TabOrganizationController::ApplyTasks() {
  std::vector<int> ungroup;
  for (const Page& page : tree_.pages()) {
    const tabs::TabInterface* tab = TabFor(page.id);
    if (tab && page.task.empty() && tab->GetGroup()) {
      ungroup.push_back(tabs_->GetIndexOfTab(tab));
    }
  }
  if (!ungroup.empty()) {
    std::ranges::sort(ungroup);
    tabs_->RemoveFromGroup(ungroup);
  }

  for (const std::string& task : tree_.Tasks()) {
    std::vector<int> indices;
    for (const Page& page : tree_.pages()) {
      const tabs::TabInterface* tab = TabFor(page.id);
      if (tab && page.task == task && TaskOf(tab) != task) {
        indices.push_back(tabs_->GetIndexOfTab(tab));
      }
    }
    if (indices.empty()) {
      continue;
    }
    std::ranges::sort(indices);
    if (const std::optional<tab_groups::TabGroupId> group = GroupFor(task)) {
      tabs_->AddToExistingGroup(indices, *group, /*add_to_end=*/true);
      continue;
    }
    const auto pending = pending_tasks_.find(task);
    const std::optional<base::Token> token = base::Token::FromString(task);
    if (pending == pending_tasks_.end() || !token) {
      continue;
    }
    const tab_groups::TabGroupId group =
        tab_groups::TabGroupId::FromRawToken(*token);
    tabs_->AddToGroupForRestore(indices, group);
    const tab_groups::TabGroupVisualData* created =
        tabs_->group_model()->GetTabGroup(group)->visual_data();
    tabs_->ChangeTabGroupVisuals(
        group,
        pending->second.visuals.value_or(tab_groups::TabGroupVisualData(
            pending->second.title, created->color())),
        /*is_customized=*/true);
  }
  pending_tasks_.clear();
}

void TabOrganizationController::ApplyOrder() {
  for (int i = 0; i < tabs_->count(); ++i) {
    if (tabs_->GetTabAtIndex(i)->IsSplit()) {
      return;
    }
  }
  int next = tabs_->IndexOfFirstNonPinnedTab();
  for (const std::string& task : tree_.Tasks()) {
    const std::optional<tab_groups::TabGroupId> group = GroupFor(task);
    if (!group) {
      continue;
    }
    const TabGroup* native = tabs_->group_model()->GetTabGroup(*group);
    if (static_cast<int>(native->ListTabs().start()) != next) {
      tabs_->MoveGroupTo(*group, next);
    }
    for (const Page& page : tree_.pages()) {
      const tabs::TabInterface* tab = TabFor(page.id);
      if (!tab || page.task != task || tab->GetGroup() != group) {
        continue;
      }
      const int index = tabs_->GetIndexOfTab(tab);
      if (index != next) {
        tabs_->MoveWebContentsAt(index, next, /*select_after_move=*/false,
                                 group);
      }
      ++next;
    }
    next = static_cast<int>(native->ListTabs().end());
  }
  for (const Page& page : tree_.pages()) {
    const tabs::TabInterface* tab = TabFor(page.id);
    if (!tab || !page.task.empty() || tab->GetGroup()) {
      continue;
    }
    const int index = tabs_->GetIndexOfTab(tab);
    if (index != next) {
      tabs_->MoveWebContentsAt(index, next, /*select_after_move=*/false,
                               std::nullopt);
    }
    ++next;
  }
}

void TabOrganizationController::ScheduleApply() {
  if (apply_pending_) {
    return;
  }
  apply_pending_ = true;
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(&TabOrganizationController::ApplyWhenIdle,
                                weak_factory_.GetWeakPtr()));
}

void TabOrganizationController::ApplyWhenIdle() {
  apply_pending_ = false;
  // Horizontal tab drags do not report their start and end here, so the
  // strip is only put back in tree order on its own in vertical mode.
  if (dragging_ || !window_->GetProfile()->GetPrefs()->GetBoolean(
                       prefs::kVerticalTabsEnabled)) {
    return;
  }
  Apply();
}

void TabOrganizationController::Record() {
  Snapshot snapshot{tree_, {}};
  if (tabs_->SupportsTabGroups()) {
    for (const tab_groups::TabGroupId& group :
         tabs_->group_model()->ListTabGroups()) {
      snapshot.tasks.emplace(
          group.ToString(),
          *tabs_->group_model()->GetTabGroup(group)->visual_data());
    }
  }
  undo_.push_back(std::move(snapshot));
  if (undo_.size() > kMaxUndoActions) {
    undo_.erase(undo_.begin());
  }
}

void TabOrganizationController::ScheduleUpdate() {
  if (update_pending_) {
    return;
  }
  update_pending_ = true;
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(&TabOrganizationController::Update,
                                weak_factory_.GetWeakPtr()));
}

void TabOrganizationController::Update() {
  update_pending_ = false;
  Profile* profile = window_->GetProfile();
  SessionService* service = profile->IsOffTheRecord()
                                ? nullptr
                                : SessionServiceFactory::GetForProfile(profile);
  std::erase_if(generated_names_,
                [this](const auto& entry) { return !GroupFor(entry.first); });
  std::map<PageId, std::string> states;
  for (const auto& [handle, id] : ids_) {
    const Page* page = tree_.Find(id);
    const tabs::TabInterface* tab = handle.Get();
    content::WebContents* contents = tab ? tab->GetContents() : nullptr;
    if (!page || !contents) {
      continue;
    }
    base::DictValue state;
    state.Set("id", id.value());
    state.Set("parent", page->parent.value());
    if (HasGeneratedName(page->task)) {
      state.Set("task_name", generated_names_[page->task]);
    }
    std::string& json = states[id];
    json = base::WriteJson(state).value_or(std::string());
    const auto saved = saved_states_.find(id);
    if (service && (saved == saved_states_.end() || saved->second != json)) {
      service->AddTabExtraData(window_->GetSessionID(),
                               sessions::SessionTabHelper::IdForTab(contents),
                               kSessionKey, json);
    }
  }
  saved_states_ = std::move(states);
  fresh_.clear();
  changed_.Notify();
}

}  // namespace uw
