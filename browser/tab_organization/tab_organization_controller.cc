// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/browser/tab_organization/tab_organization_controller.h"

#include <algorithm>
#include <cstdlib>
#include <functional>
#include <utility>

#include "base/auto_reset.h"
#include "base/barrier_callback.h"
#include "base/feature_list.h"
#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/sequenced_task_runner.h"
#include "base/token.h"
#include "base/uuid.h"
#include "base/values.h"
#include "chrome/browser/lifetime/browser_shutdown.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/sessions/session_service.h"
#include "chrome/browser/sessions/session_service_factory.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/tabs/tab_enums.h"
#include "chrome/browser/ui/tabs/tab_group_model.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/common/webui_url_constants.h"
#include "chrome/grit/generated_resources.h"
#include "components/prefs/pref_service.h"
#include "components/sessions/content/session_tab_helper.h"
#include "components/tabs/public/tab_group.h"
#include "content/public/browser/web_contents.h"
#include "ui/base/base_window.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/models/list_selection_model.h"
#include "ui/menus/simple_menu_model.h"
#include "url/gurl.h"
#include "uw/browser/tab_organization/ai_connection.h"
#include "uw/browser/tab_organization/shared_tree.h"
#include "uw/browser/terminal/terminal_shell.h"
#include "uw/browser/workspaces/workspace_window.h"

namespace uw {
namespace {

constexpr size_t kMaxUndoActions = 100;

bool Has(const std::vector<PageId>& ids, const PageId& id) {
  return std::ranges::find(ids, id) != ids.end();
}

// Sideways movement counts once it passes a dead zone, so a wobbly drag keeps
// its depth. Then each indent is a level.
int LevelsMoved(int dx) {
  constexpr int kDeadZone = 12;
  const int levels =
      (std::abs(dx) + kIndentPerLevel - kDeadZone) / kIndentPerLevel;
  return dx < 0 ? -levels : levels;
}

}  // namespace

DEFINE_USER_DATA(TabOrganizationController);

TabOrganizationController::TabOrganizationController(
    BrowserWindowInterface* window)
    : HiddenTabs(window->GetUnownedUserDataHost()),
      window_(window),
      tabs_(window->GetTabStripModel()),
      shared_(base::MakeRefCounted<SharedTree>()),
      user_data_(window->GetUnownedUserDataHost(), *this) {
  tabs_->AddObserver(this);
  Mirror({});
  active_ = IdFor(tabs_->GetActiveTab());
  Remember(active_);
}

TabOrganizationController::~TabOrganizationController() {
  tabs_->RemoveObserver(this);
  if (joined_) {
    shared_->Remove(this);
  }
}

const PageTree& TabOrganizationController::tree() const {
  return shared_->tree();
}

bool TabOrganizationController::WasOpenedHere(const PageId& id) const {
  return shared_->opened().contains(id);
}

bool TabOrganizationController::CanUndo() const {
  return !shared_->undo().empty();
}

content::WebContents* TabOrganizationController::PageContents(
    const PageId& id) const {
  if (joined_) {
    if (content::WebContents* page = shared_->PageOf(id, nullptr)) {
      return page;
    }
  }
  const tabs::TabInterface* tab = TabFor(id);
  return tab ? tab->GetContents() : nullptr;
}

std::vector<BrowserWindowInterface*> TabOrganizationController::Windows()
    const {
  if (!joined_) {
    return {window_};
  }
  std::vector<BrowserWindowInterface*> windows;
  for (const TabOrganizationController* window : shared_->windows()) {
    windows.push_back(window->window_);
  }
  return windows;
}

std::string TabOrganizationController::TaskOf(
    const tabs::TabInterface* tab) const {
  const std::optional<tab_groups::TabGroupId> group = tab->GetGroup();
  return group ? KeyOf(*group) : std::string();
}

std::string TabOrganizationController::KeyOf(
    const tab_groups::TabGroupId& group) const {
  const auto key = keys_.find(group);
  return key == keys_.end() ? group.ToString() : key->second;
}

// static
std::string TabOrganizationController::TaskKey(
    BrowserWindowInterface* window,
    const tab_groups::TabGroupId& group) {
  const TabOrganizationController* self = From(window);
  return self ? self->KeyOf(group) : group.ToString();
}

void TabOrganizationController::JoinWorkspace() {
  const WorkspaceWindow* workspace = WorkspaceWindow::From(window_);
  if (joined_ || !base::FeatureList::IsEnabled(kUwSharedTabs) ||
      tabs_->closing_all() || !workspace || workspace->workspace().empty()) {
    return;
  }
  scoped_refptr<SharedTree> shared = SharedTree::For(workspace->workspace());
  if (std::ranges::any_of(shared->windows(), [](const auto& window) {
        return window->dragging();
      })) {
    shared->WaitToJoin(this);
    return;
  }
  // A new window on a workspace with tabs, as from Cmd+N, opens with a blank
  // tab that stays only if used, and with its command bar.
  std::optional<PageId> blank;
  if (shared_->tree().pages().size() == 1 && !shared->tree().pages().empty()) {
    const PageId& only = shared_->tree().pages().front().id;
    if (shared_->opened().contains(only) && IsBlank(only)) {
      blank = only;
    }
  }
  shared->Merge(*shared_);
  shared_ = std::move(shared);
  shared_->Add(this);
  joined_ = true;
  unused_blank_ = blank;
  // A window opened by moving tabs out of another, as by dragging them out,
  // puts them back in their place in the tree.
  if (opened_from_) {
    shared_->tree().Restore(*opened_from_);
    opened_from_.reset();
  }
  reveals_ = WorkspaceWindow::ObserveReveals(base::BindRepeating(
      &TabOrganizationController::OnRevealed, base::Unretained(this)));
  Apply();
  if (blank && !WorkspaceWindow::IsHidden(window_)) {
    command_bar_requests_.Notify();
  }
  // A window kept for the workspace's tabs closes once another one shows
  // them. Its live pages go to that window first.
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(
                     [](base::WeakPtr<TabOrganizationController> self) {
                       if (!self || !self->joined_) {
                         return;
                       }
                       std::vector<BrowserWindowInterface*> keepers;
                       bool shown = false;
                       for (BrowserWindowInterface* window : self->Windows()) {
                         const WorkspaceWindow* workspace =
                             WorkspaceWindow::From(window);
                         if (workspace && workspace->keeper()) {
                           keepers.push_back(window);
                         } else {
                           shown = true;
                         }
                       }
                       if (!shown) {
                         return;
                       }
                       for (BrowserWindowInterface* keeper : keepers) {
                         keeper->GetWindow()->Close();
                       }
                     },
                     weak_factory_.GetWeakPtr()));
}

void TabOrganizationController::Leave() {
  shared_->Remove(this);
  shared_ = base::MakeRefCounted<SharedTree>();
  joined_ = false;
  reveals_ = {};
}

void TabOrganizationController::Follow() {
  if (dragging_ || tabs_->closing_all()) {
    return;
  }
  if (observing_) {
    ScheduleOwnApply();
    return;
  }
  Apply();
}

std::optional<tab_groups::TabGroupVisualData>
TabOrganizationController::VisualsOf(const std::string& task) const {
  const std::optional<tab_groups::TabGroupId> group = GroupFor(task);
  if (!group) {
    return std::nullopt;
  }
  return *tabs_->group_model()->GetTabGroup(*group)->visual_data();
}

void TabOrganizationController::SetTaskVisuals(
    const std::string& task,
    const tab_groups::TabGroupVisualData& visuals) {
  const std::optional<tab_groups::TabGroupId> group = GroupFor(task);
  if (!group ||
      *tabs_->group_model()->GetTabGroup(*group)->visual_data() == visuals) {
    return;
  }
  base::AutoReset<bool> guard(&applying_, true);
  tabs_->ChangeTabGroupVisuals(*group, visuals, /*is_customized=*/true);
}

std::unique_ptr<content::WebContents> TabOrganizationController::ReplacePage(
    tabs::TabInterface* tab,
    std::unique_ptr<content::WebContents> page) {
  base::AutoReset<bool> guard(&applying_, true);
  return tabs_->DiscardWebContents(tab->GetContents(), std::move(page));
}

// static
bool TabOrganizationController::IsCopy(const tabs::TabInterface* tab) {
  return tab && SharedTree::IsCopy(tab->GetContents());
}

void TabOrganizationController::BringLivePage(tabs::TabInterface* tab) {
  const std::optional<PageId> id = IdFor(tab);
  if (joined_ && id) {
    shared_->BringLivePage(this, *id);
  }
}

void TabOrganizationController::BringSelectedPage(bool chosen) {
  tabs::TabInterface* tab = tabs_->GetActiveTab();
  const std::optional<PageId> id = IdFor(tab);
  if (!joined_ || !id || !IsCopy(tab) || WorkspaceWindow::IsHidden(window_)) {
    return;
  }
  const TabOrganizationController* holder = shared_->HolderOf(*id, this);
  if (!chosen && holder && !WorkspaceWindow::IsHidden(holder->window_)) {
    return;
  }
  shared_->BringLivePage(this, *id);
}

// A window that comes to the front with a copy selected shows the live page,
// unless another shown window holds it.
void TabOrganizationController::OnRevealed(BrowserWindowInterface* window) {
  if (window == window_) {
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE,
        base::BindOnce(&TabOrganizationController::BringSelectedPage,
                       weak_factory_.GetWeakPtr(), /*chosen=*/false));
  }
}

gfx::ImageSkia TabOrganizationController::PictureOf(
    const tabs::TabInterface* tab) const {
  const std::optional<PageId> id = IdFor(tab);
  return id ? shared_->PictureOf(*id) : gfx::ImageSkia();
}

base::CallbackListSubscription TabOrganizationController::ObservePictures(
    base::RepeatingClosure callback) {
  return pictures_changed_.Add(std::move(callback));
}

void TabOrganizationController::NotifyPictures() {
  pictures_changed_.Notify();
}

base::CallbackListSubscription
TabOrganizationController::ObserveCommandBarRequests(
    base::RepeatingClosure callback) {
  return command_bar_requests_.Add(std::move(callback));
}

base::WeakPtr<TabOrganizationController>
TabOrganizationController::GetWeakPtr() {
  return weak_factory_.GetWeakPtr();
}

void TabOrganizationController::CloseLeaving() {
  base::AutoReset<bool> guard(&applying_, true);
  for (const tabs::TabHandle& handle : leaving_) {
    const tabs::TabInterface* tab = handle.Get();
    const int index = tab ? tabs_->GetIndexOfTab(tab) : TabStripModel::kNoTab;
    if (index != TabStripModel::kNoTab) {
      tabs_->CloseWebContentsAt(index, TabCloseTypes::CLOSE_NONE);
    }
  }
  leaving_.clear();
}

bool TabOrganizationController::IsBlank(const PageId& id) const {
  const tabs::TabInterface* tab = TabFor(id);
  content::WebContents* page = tab ? tab->GetContents() : nullptr;
  if (!page || page->GetController().GetEntryCount() > 1) {
    return false;
  }
  const GURL& url = page->GetVisibleURL();
  return url.is_empty() || url.IsAboutBlank() ||
         url == GURL(chrome::kChromeUINewTabURL) ||
         url.host() == chrome::kChromeUINewTabPageHost;
}

void TabOrganizationController::CloseUnusedBlank(bool closing) {
  if (!unused_blank_) {
    return;
  }
  tabs::TabInterface* tab = TabFor(*unused_blank_);
  if (!tab || !IsBlank(*unused_blank_)) {
    unused_blank_.reset();
    return;
  }
  // It is still the one shown, as after the user picked it again, or the
  // window's last tab.
  if ((!closing && tab == tabs_->GetActiveTab()) || tabs_->count() == 1) {
    return;
  }
  unused_blank_.reset();
  tabs_->CloseWebContentsAt(tabs_->GetIndexOfTab(tab),
                            TabCloseTypes::CLOSE_NONE);
}

bool TabOrganizationController::IsCommand(int command) {
  return command >= kNewTask && command <= kLastCommand;
}

void TabOrganizationController::AddCommands(
    ui::SimpleMenuModel* menu,
    const std::vector<PageId>& ids) const {
  // The labels are title case, as Chromium's are in menus on the Mac.
  // Chromium's own items for these repeat Move to new task and Move to
  // Scratchpad.
  for (int command : {TabStripModel::CommandAddToNewGroup,
                      TabStripModel::CommandRemoveFromGroup}) {
    if (const std::optional<size_t> index =
            menu->GetIndexOfCommandId(command)) {
      menu->RemoveItemAt(*index);
    }
  }
  // The first of `commands` the menu has.
  const auto find =
      [menu](std::initializer_list<int> commands) -> std::optional<size_t> {
    for (int command : commands) {
      if (const std::optional<size_t> index =
              menu->GetIndexOfCommandId(command)) {
        return index;
      }
    }
    return std::nullopt;
  };
  const auto insert = [menu](size_t& at, int command, int string) {
    menu->InsertItemAt(at++, command, l10n_util::GetStringUTF16(string));
  };

  if (const std::optional<size_t> below =
          find({TabStripModel::CommandNewTabToRight})) {
    size_t at = *below + 1;
    insert(at, kNewChildTab, IDS_UW_NEW_CHILD_TAB_MAC);
  }

  // The moves sit after Move to task and before Move to another window, and
  // Keep here after them. The tree's own commands follow.
  const auto window = [&] {
    return find({TabStripModel::CommandMoveToExistingWindow,
                 TabStripModel::CommandMoveTabsToNewWindow});
  };
  const std::optional<size_t> to_task =
      find({TabStripModel::CommandAddToExistingGroup});
  size_t at = to_task ? *to_task + 1 : window().value_or(menu->GetItemCount());
  insert(at, kNewTask, IDS_UW_MOVE_TO_NEW_TASK_MAC);
  insert(at, kScratchpad, IDS_UW_MOVE_TO_SCRATCHPAD_MAC);
  if (const std::optional<size_t> moves_window = window()) {
    at = std::max(at, *moves_window + 1);
  }
  insert(at, kKeepHere,
         KeepsTabs(ids) ? IDS_UW_KEEP_HERE_MAC : IDS_UW_STOP_KEEPING_HERE_MAC);
  menu->InsertSeparatorAt(at++, ui::NORMAL_SEPARATOR);
  insert(at, kNest, IDS_UW_NEST_BRANCH_MAC);
  insert(at, kOutdent, IDS_UW_OUTDENT_BRANCH_MAC);
  insert(at, kToggleBranch,
         CollapsesBranches(ids) ? IDS_UW_COLLAPSE_BRANCH_MAC
                                : IDS_UW_EXPAND_BRANCH_MAC);
  insert(at, kUndo, IDS_UW_UNDO_ORGANIZATION_MAC);

  // Close branch sits under Close tab.
  if (const std::optional<size_t> close =
          find({TabStripModel::CommandCloseTab})) {
    at = *close + 1;
  } else {
    menu->AddSeparator(ui::NORMAL_SEPARATOR);
    at = menu->GetItemCount();
  }
  insert(at, kCloseBranch, IDS_UW_CLOSE_BRANCH_MAC);
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

// static
bool TabOrganizationController::KeepsWindow(BrowserWindowInterface* window) {
  TabOrganizationController* self = From(window);
  WorkspaceWindow* workspace = WorkspaceWindow::From(window);
  if (!self || !self->joined_ || !workspace ||
      browser_shutdown::IsTryingToQuit()) {
    return false;
  }
  self->CloseUnusedBlank(/*closing=*/true);
  if (self->shared_->windows().size() > 1) {
    self->shared_->HandOver(self);
    return false;
  }
  if (workspace->forgetting() || workspace->keeper() ||
      self->shared_->tree().pages().empty()) {
    return false;
  }
  workspace->Keep();
  self->shared_->UnloadIdlePages();
  return true;
}

// static
bool TabOrganizationController::ClosesNoTabs(BrowserWindowInterface* window) {
  const TabOrganizationController* self = From(window);
  return self && self->joined_;
}

bool TabOrganizationController::OpenNewTask(BrowserWindowInterface* window) {
  TabOrganizationController* self = From(window);
  if (!self) {
    return false;
  }
  chrome::NewTab(window, NewTabTypes::kNewTabCommand);
  if (const std::optional<PageId> page =
          self->IdFor(self->tabs_->GetActiveTab())) {
    const std::optional<tab_groups::TabGroupId> task = self->MoveToNewTask(
        {*page}, l10n_util::GetStringUTF16(IDS_UW_UNTITLED_TASK));
    // The user made the task, so it opens ready for its name.
    if (task) {
      self->RenameOnItsRow(self->KeyOf(*task));
    }
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
  self->shared_->opened().insert(*id);
  if (source) {
    if (const std::optional<PageId> parent =
            self->IdFor(self->tabs_->GetTabForWebContents(source))) {
      self->shared_->tree().SetParent(*id, *parent);
    }
  } else if (const std::optional<PageId>& beside =
                 self->active_ == id ? self->previous_active_ : self->active_) {
    self->shared_->tree().OpenBeside(*id, *beside);
  }
  self->RevealSelected();
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
  if (state->FindBool("collapsed").value_or(false)) {
    self->shared_->collapsed().insert(PageId(*id));
  }
  if (state->FindBool("kept").value_or(false)) {
    self->shared_->kept()[PageId(*id)] = self->TaskOf(tab);
  }
  if (state->FindBool("held").value_or(false) && parent && !parent->empty()) {
    self->shared_->held()[PageId(*id)] = PageId(*parent);
  }
  const std::string* task_name = state->FindString("task_name");
  if (task_name && tab->GetGroup()) {
    self->shared_->generated_names().try_emplace(self->TaskOf(tab),
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
  return id ? shared_->tree().DepthOf(*id) : 0;
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

std::vector<PageId> TabOrganizationController::TargetsFor(
    int command,
    const tabs::TabInterface* context) const {
  if (command != kNewChildTab) {
    return SelectionFor(context);
  }
  const std::optional<PageId> id =
      IdFor(context ? context : tabs_->GetActiveTab());
  return id ? std::vector<PageId>{*id} : std::vector<PageId>();
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
    case kNewChildTab:
      return ids.size() == 1;
    case kKeepHere:
      return true;
    case kNewTask:
      return tabs_->SupportsTabGroups();
    case kScratchpad:
      return std::ranges::any_of(roots, [this](const PageId& id) {
        const Page* page = shared_->tree().Find(id);
        return !page->task.empty() || !page->parent.value().empty();
      });
    case kNest:
      return roots.size() == 1 &&
             shared_->tree().PreviousSibling(roots.front());
    case kOutdent:
      return roots.size() == 1 &&
             !shared_->tree().Find(roots.front())->parent.value().empty();
    case kMoveUp:
      return roots.size() == 1 &&
             shared_->tree().PreviousSibling(roots.front());
    case kMoveDown:
      return roots.size() == 1 && shared_->tree().NextSibling(roots.front());
    case kCloseBranch:
      return !Parents(ids).empty();
    case kToggleBranch:
      return !Parents(ids).empty();
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
    case kNewChildTab:
      OpenChild(ids.front());
      break;
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
    case kToggleBranch:
      ToggleBranches(ids);
      break;
    case kKeepHere:
      ToggleKept(ids);
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
  Record(roots, task);
  LeaveBehind(roots, staying);
  shared_->tree().MoveToNewTask(roots.front(), task);
  for (size_t i = 1; i < roots.size(); ++i) {
    shared_->tree().MoveToTask(roots[i], task);
  }
  if (origin == Origin::kOrganizer) {
    shared_->generated_names()[task] = title;
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
  const std::string task = KeyOf(group);
  if (roots.empty() || !GroupFor(task)) {
    return false;
  }
  Record(roots, task);
  LeaveBehind(roots, staying);
  for (const PageId& root : roots) {
    shared_->tree().MoveToTask(root, task);
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
  Record(roots, std::string());
  LeaveBehind(roots, staying);
  for (const PageId& root : roots) {
    shared_->tree().MoveToTask(root, std::string());
  }
  Apply();
  if (origin == Origin::kUser) {
    NotifyManualMove(roots);
  }
  return true;
}

bool TabOrganizationController::ApplyTidy(const std::vector<TidyMove>& moves) {
  if (moves.empty() || !tabs_->SupportsTabGroups()) {
    return false;
  }
  std::vector<PageId> tops;
  for (const TidyMove& move : moves) {
    tops.push_back(move.top);
  }
  Record(tops, std::nullopt);
  std::map<std::u16string, std::string> started;
  for (const TidyMove& move : moves) {
    if (!shared_->tree().Find(move.top)) {
      continue;
    }
    LeaveBehind({move.top}, move.staying);
    if (!move.target.task.empty()) {
      shared_->tree().MoveToTask(move.top, move.target.task);
      continue;
    }
    auto [task, fresh] = started.try_emplace(move.target.new_task);
    if (!fresh) {
      shared_->tree().MoveToTask(move.top, task->second);
      continue;
    }
    task->second = tab_groups::TabGroupId::GenerateNew().ToString();
    shared_->tree().MoveToNewTask(move.top, task->second);
    shared_->generated_names()[task->second] = move.target.new_task;
    pending_tasks_[task->second] = {move.target.new_task, std::nullopt};
  }
  Apply();
  return true;
}

bool TabOrganizationController::Nest(const PageId& id) {
  Record({id});
  if (!shared_->tree().Nest(id)) {
    shared_->undo().pop_back();
    return false;
  }
  shared_->held()[id] = shared_->tree().Find(id)->parent;
  Apply();
  NotifyManualMove({id});
  return true;
}

void TabOrganizationController::OpenChild(const PageId& id) {
  if (const std::optional<PageId> page = OpenBlankTab(id)) {
    shared_->tree().OpenUnder(*page, id);
    shared_->held()[*page] = id;
    RevealSelected();
    ScheduleApply();
    ScheduleUpdate();
  }
}

void TabOrganizationController::OpenBelow(const PageId& id) {
  if (const std::optional<PageId> page = OpenBlankTab(id)) {
    shared_->tree().OpenBeside(*page, id);
    ScheduleApply();
    ScheduleUpdate();
  }
}

std::optional<PageId> TabOrganizationController::OpenBlankTab(
    const PageId& anchor) {
  tabs::TabInterface* tab = TabFor(anchor);
  if (!tab) {
    return std::nullopt;
  }
  content::WebContents* page =
      chrome::AddAndReturnTabAt(window_, GURL(), tabs_->GetIndexOfTab(tab) + 1,
                                /*foreground=*/true, tab->GetGroup());
  return page ? IdFor(tabs_->GetTabForWebContents(page)) : std::nullopt;
}

bool TabOrganizationController::Outdent(const PageId& id) {
  Record({id});
  if (!shared_->tree().Outdent(id)) {
    shared_->undo().pop_back();
    return false;
  }
  Apply();
  NotifyManualMove({id});
  return true;
}

bool TabOrganizationController::MoveUp(const PageId& id) {
  Record({id});
  if (!shared_->tree().MoveUp(id)) {
    shared_->undo().pop_back();
    return false;
  }
  Apply();
  NotifyManualMove({id});
  return true;
}

bool TabOrganizationController::MoveDown(const PageId& id) {
  Record({id});
  if (!shared_->tree().MoveDown(id)) {
    shared_->undo().pop_back();
    return false;
  }
  Apply();
  NotifyManualMove({id});
  return true;
}

void TabOrganizationController::CloseBranches(const std::vector<PageId>& ids) {
  std::vector<PageId> closing;
  for (const PageId& root : Roots(ids)) {
    const std::vector<PageId> branch = shared_->tree().BranchOf(root);
    closing.insert(closing.end(), branch.begin(), branch.end());
  }
  Close(std::move(closing));
}

void TabOrganizationController::FindClearable(
    base::OnceCallback<void(std::vector<PageId>)> done) const {
  std::vector<PageId> pages;
  for (const Page& page : shared_->tree().pages()) {
    if (page.task.empty() && !IsKept(page.id)) {
      pages.push_back(page.id);
    }
  }
  const auto answered = base::BarrierCallback<std::optional<PageId>>(
      pages.size(), base::BindOnce(
                        [](base::OnceCallback<void(std::vector<PageId>)> done,
                           std::vector<std::optional<PageId>> answers) {
                          std::vector<PageId> clearable;
                          for (std::optional<PageId>& answer : answers) {
                            if (answer) {
                              clearable.push_back(std::move(*answer));
                            }
                          }
                          std::move(done).Run(std::move(clearable));
                        },
                        std::move(done)));
  for (const PageId& id : pages) {
    tabs::TabInterface* tab = TabFor(id);
    TerminalShell* shell =
        tab ? TerminalShell::From(tab->GetContents()) : nullptr;
    if (!shell) {
      answered.Run(id);
      continue;
    }
    // A terminal is running a command while a program other than its shell
    // has it.
    shell->GetActivity(base::BindOnce(
        [](base::RepeatingCallback<void(std::optional<PageId>)> answered,
           PageId id, ShellHost::Activity activity) {
          answered.Run(activity.program.empty() ? std::optional(std::move(id))
                                                : std::nullopt);
        },
        answered, id));
  }
}

void TabOrganizationController::ClearScratchpad() {
  FindClearable(base::BindOnce(
      [](base::WeakPtr<TabOrganizationController> self,
         std::vector<PageId> ids) {
        if (!self) {
          return;
        }
        // A tab may have moved or been kept while the shells answered.
        std::erase_if(ids, [&self](const PageId& id) {
          const Page* page = self->shared_->tree().Find(id);
          return !page || !page->task.empty() || self->IsKept(id);
        });
        if (ids.empty()) {
          return;
        }
        if (ids.size() ==
            static_cast<size_t>(self->tabs_->count() -
                                self->tabs_->IndexOfFirstNonPinnedTab())) {
          chrome::AddTabAt(self->window_, GURL(), -1, /*foreground=*/true);
        }
        self->Close(std::move(ids));
      },
      weak_factory_.GetWeakPtr()));
}

void TabOrganizationController::Close(std::vector<PageId> ids) {
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
          weak_factory_.GetWeakPtr(), std::move(ids)),
      false);
}

void TabOrganizationController::CloseTask(const tab_groups::TabGroupId& task) {
  if (!tabs_->SupportsTabGroups() ||
      !tabs_->group_model()->ContainsTabGroup(task)) {
    return;
  }
  if (tabs_->count() - tabs_->IndexOfFirstNonPinnedTab() ==
      static_cast<int>(tabs_->group_model()->GetTabGroup(task)->tab_count())) {
    chrome::AddTabAt(window_, GURL(), -1, /*foreground=*/true);
  }
  tabs_->CloseAllTabsInGroup(task);
}

void TabOrganizationController::OpenInTask(const tab_groups::TabGroupId& task) {
  if (!tabs_->SupportsTabGroups() ||
      !tabs_->group_model()->ContainsTabGroup(task)) {
    return;
  }
  const int end = static_cast<int>(
      tabs_->group_model()->GetTabGroup(task)->ListTabs().end());
  content::WebContents* page = chrome::AddAndReturnTabAt(
      window_, GURL(), end, /*foreground=*/true, task);
  if (page) {
    PlaceInTask(tabs_->GetTabForWebContents(page), task);
  }
}

void TabOrganizationController::PlaceInTask(
    const tabs::TabInterface* tab,
    const tab_groups::TabGroupId& task) {
  const std::optional<PageId> id = IdFor(tab);
  if (!id || !tabs_->SupportsTabGroups() ||
      !tabs_->group_model()->ContainsTabGroup(task)) {
    return;
  }
  if (tabs_->IsGroupCollapsed(task)) {
    const tab_groups::TabGroupVisualData* visuals =
        tabs_->group_model()->GetTabGroup(task)->visual_data();
    tabs_->ChangeTabGroupVisuals(
        task, tab_groups::TabGroupVisualData(visuals->title(), visuals->color(),
                                             /*is_collapsed=*/false));
  }
  shared_->tree().MoveToTask(*id, KeyOf(task));
  RevealSelected();
  ScheduleApply();
  ScheduleUpdate();
}

bool TabOrganizationController::IsCollapsed(const PageId& id) const {
  return shared_->collapsed().contains(id);
}

bool TabOrganizationController::IsHidden(const PageId& id) const {
  const Page* page = shared_->tree().Find(id);
  for (const Page* up = page ? shared_->tree().Find(page->parent) : nullptr; up;
       up = shared_->tree().Find(up->parent)) {
    if (shared_->collapsed().contains(up->id)) {
      return true;
    }
  }
  return false;
}

bool TabOrganizationController::IsHidden(const tabs::TabInterface* tab) const {
  const std::optional<PageId> id = IdFor(tab);
  return id && IsHidden(*id);
}

bool TabOrganizationController::CollapsesBranches(
    const std::vector<PageId>& ids) const {
  const std::vector<PageId> parents = Parents(ids);
  return parents.empty() ||
         std::ranges::any_of(parents, [this](const PageId& id) {
           return !shared_->collapsed().contains(id);
         });
}

bool TabOrganizationController::IsKept(const PageId& id) const {
  const auto kept = shared_->kept().find(id);
  const Page* page = shared_->tree().Find(id);
  return kept != shared_->kept().end() && page && page->task == kept->second;
}

bool TabOrganizationController::KeepsTabs(
    const std::vector<PageId>& ids) const {
  return std::ranges::any_of(ids,
                             [this](const PageId& id) { return !IsKept(id); });
}

void TabOrganizationController::ToggleKept(const std::vector<PageId>& ids) {
  if (KeepsTabs(ids)) {
    for (const PageId& id : ids) {
      if (const Page* page = shared_->tree().Find(id)) {
        shared_->kept()[id] = page->task;
      }
    }
  } else {
    for (const PageId& id : ids) {
      shared_->kept().erase(id);
    }
  }
  ScheduleUpdate();
}

bool TabOrganizationController::IsHeld(const PageId& id) const {
  const auto held = shared_->held().find(id);
  const Page* page = shared_->tree().Find(id);
  return held != shared_->held().end() && page && page->parent == held->second;
}

void TabOrganizationController::ToggleBranches(const std::vector<PageId>& ids) {
  const std::vector<PageId> parents = Parents(ids);
  if (parents.empty()) {
    return;
  }
  if (!CollapsesBranches(ids)) {
    for (const PageId& id : parents) {
      shared_->collapsed().erase(id);
    }
  } else {
    shared_->collapsed().insert(parents.begin(), parents.end());
    const std::optional<PageId> selected = IdFor(tabs_->GetActiveTab());
    if (selected && IsHidden(*selected)) {
      PageId top;
      for (const Page* up =
               shared_->tree().Find(shared_->tree().Find(*selected)->parent);
           up; up = shared_->tree().Find(up->parent)) {
        if (shared_->collapsed().contains(up->id)) {
          top = up->id;
        }
      }
      tabs_->ActivateTabAt(tabs_->GetIndexOfTab(TabFor(top)));
    }
  }
  NotifyManualMove(parents);
  ScheduleUpdate();
}

bool TabOrganizationController::SelectTask(const std::string& task) {
  const auto in_task = [&](const PageId& id) {
    const Page* page = shared_->tree().Find(id);
    return page && page->task == task;
  };
  auto page = std::ranges::find_if(recent_, in_task);
  std::optional<PageId> target;
  if (page != recent_.end()) {
    target = *page;
  } else if (auto first =
                 std::ranges::find(shared_->tree().pages(), task, &Page::task);
             first != shared_->tree().pages().end()) {
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
    return page == id || !shared_->tree().Find(page);
  });
  if (id) {
    recent_.insert(recent_.begin(), *id);
  }
}

void TabOrganizationController::RenameOnItsRow(const std::string& task) {
  rename_ = task;
  renames_.Notify();
}

bool TabOrganizationController::TakeRename(const std::string& task) {
  if (task.empty() || rename_ != task) {
    return false;
  }
  rename_.clear();
  return true;
}

base::CallbackListSubscription TabOrganizationController::ObserveRenames(
    base::RepeatingClosure callback) {
  return renames_.Add(std::move(callback));
}

std::optional<TabOrganizationController::UndoMove>
TabOrganizationController::NextUndo() const {
  if (shared_->undo().empty()) {
    return std::nullopt;
  }
  const SharedTree::Step& snapshot = shared_->undo().back();
  UndoMove move{snapshot.tab, {}};
  if (snapshot.task) {
    move.place = snapshot.task->empty()
                     ? l10n_util::GetStringUTF16(IDS_UW_SCRATCHPAD)
                     : TaskTitle(*snapshot.task);
  }
  return move;
}

void TabOrganizationController::Undo() {
  if (shared_->undo().empty()) {
    return;
  }
  SharedTree::Step snapshot = std::move(shared_->undo().back());
  shared_->undo().pop_back();
  const PageTree before = shared_->tree();
  shared_->tree().Restore(snapshot.tree);
  for (const std::string& task : shared_->tree().Tasks()) {
    const auto visuals = snapshot.tasks.find(task);
    if (!GroupFor(task) && visuals != snapshot.tasks.end()) {
      pending_tasks_[task] = {std::u16string(), visuals->second};
    }
  }
  Apply();
  // A restored placement counts as the user's own.
  std::vector<PageId> restored;
  for (const Page& page : shared_->tree().pages()) {
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

void TabOrganizationController::BeginTabDrag(bool alone) {
  dragging_ = true;
  drag_alone_ = alone;
  drag_selection_.clear();
  drag_carried_.clear();
  drag_before_ = shared_->tree();
  drag_x_.reset();
  drag_levels_ = 0;
}

std::vector<tabs::TabInterface*> TabOrganizationController::DraggedTabs(
    std::vector<tabs::TabInterface*> tabs) {
  std::set<tabs::TabInterface*> dragged(tabs.begin(), tabs.end());
  for (const tabs::TabInterface* tab : tabs) {
    const std::optional<PageId> id = IdFor(tab);
    if (!id || drag_alone_) {
      continue;
    }
    for (const PageId& page : shared_->tree().BranchOf(*id)) {
      if (tabs::TabInterface* in_branch = TabFor(page)) {
        dragged.insert(in_branch);
      }
    }
  }
  for (const tabs::TabInterface* tab : dragged) {
    if (const std::optional<PageId> id = IdFor(tab)) {
      drag_carried_.insert(*id);
    }
  }
  if (dragged.size() == tabs.size()) {
    return tabs;
  }
  drag_selection_.clear();
  for (const tabs::TabInterface* tab : tabs) {
    drag_selection_.push_back(tab->GetHandle());
  }
  std::vector<tabs::TabInterface*> in_order(dragged.begin(), dragged.end());
  std::ranges::sort(in_order, {}, [this](const tabs::TabInterface* tab) {
    return tabs_->GetIndexOfTab(tab);
  });
  return in_order;
}

void TabOrganizationController::ContinueTabDrag(int x) {
  if (!dragging_ || drag_carried_.empty() ||
      !tabs_->profile()->GetPrefs()->GetBoolean(kDragSidewaysNestsTabsPref)) {
    return;
  }
  // Once the dragged tabs go on to another window, this one no longer nests
  // them, nor drops them from the tree.
  if (!std::ranges::all_of(drag_carried_,
                           [this](const PageId& id) { return TabFor(id); })) {
    return;
  }
  if (!drag_x_) {
    drag_x_ = x;
    return;
  }
  const int levels = LevelsMoved(x - *drag_x_);
  if (levels == drag_levels_) {
    return;
  }
  drag_levels_ = levels;
  Mirror(drag_carried_);
  NotifyNativeMoves(drag_carried_);
}

void TabOrganizationController::CancelTabDrag() {
  if (!dragging_) {
    return;
  }
  drag_canceled_ = true;
  drag_levels_ = 0;
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(&TabOrganizationController::EndTabDrag,
                                weak_factory_.GetWeakPtr()));
}

void TabOrganizationController::EndTabDrag() {
  if (!dragging_) {
    return;
  }
  dragging_ = false;
  if (drag_canceled_) {
    shared_->tree().Restore(drag_before_);
    ScheduleUpdate();
  } else {
    for (const PageId& id : drag_carried_) {
      const Page* now = shared_->tree().Find(id);
      const Page* then = drag_before_.Find(id);
      if (now && then && !now->parent.value().empty() &&
          now->parent != then->parent && !drag_carried_.contains(now->parent)) {
        shared_->held()[id] = now->parent;
      }
    }
  }
  // The other windows follow once Chromium has finished the drop.
  const bool settle = joined_;
  if (settle) {
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(&TabOrganizationController::SettleDrag,
                                  weak_factory_.GetWeakPtr()));
  }
  CloseIfLeft();
  drag_canceled_ = false;
  drag_carried_.clear();
  drag_before_ = PageTree();
  if (!drag_selection_.empty()) {
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE,
        base::BindOnce(&TabOrganizationController::RestoreDragSelection,
                       weak_factory_.GetWeakPtr(), std::move(drag_selection_)));
    drag_selection_.clear();
  }
  if (!settle) {
    ScheduleApply();
  }
}

void TabOrganizationController::CloseIfLeft() {
  if (opened_for_.empty() ||
      std::ranges::any_of(opened_for_,
                          [this](const PageId& id) { return TabFor(id); })) {
    return;
  }
  opened_for_.clear();
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(
                     [](base::WeakPtr<TabOrganizationController> self) {
                       if (self) {
                         self->window_->GetWindow()->Close();
                       }
                     },
                     weak_factory_.GetWeakPtr()));
}

void TabOrganizationController::SettleDrag() {
  // A window opened during the drag joins now.
  if (joined_) {
    shared_->JoinWaiting();
  }
  ScheduleApply();
}

// Puts back the selection the user had before a drag selected a whole branch.
// A click that made the selection smaller, with no drag, is left alone.
void TabOrganizationController::RestoreDragSelection(
    std::vector<tabs::TabHandle> handles) {
  ui::ListSelectionModel selection;
  const tabs::TabInterface* active = tabs_->GetActiveTab();
  for (const tabs::TabHandle& handle : handles) {
    if (const tabs::TabInterface* tab = handle.Get()) {
      const int index = tabs_->GetIndexOfTab(tab);
      if (index != TabStripModel::kNoTab) {
        selection.AddIndexToSelection(index);
        if (tab == active) {
          selection.set_active(index);
          selection.set_anchor(index);
        }
      }
    }
  }
  if (selection.empty() || !selection.active().has_value() ||
      selection.size() >=
          tabs_->selection_model().GetListSelectionModel().size()) {
    return;
  }
  tabs_->SetSelectionFromModel(selection);
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
  shared_->generated_names()[task] = title;
  tabs_->ChangeTabGroupVisuals(
      *group, tab_groups::TabGroupVisualData(title, visuals->color(),
                                             visuals->is_collapsed()));
  ScheduleUpdate();
}

bool TabOrganizationController::HasGeneratedName(
    const std::string& task) const {
  const auto generated = shared_->generated_names().find(task);
  return generated != shared_->generated_names().end() &&
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
  shared_->generated_names()[task] = TaskTitle(task);
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
  base::AutoReset<bool> observing(&observing_, true);
  // A copy the user selects shows the live page. One selected otherwise, as
  // after the selected tab closed, does so only when no shown window holds it.
  if (selection.active_tab_changed() &&
      selection.reason != TabStripModelObserver::CHANGE_REASON_REPLACED &&
      IsCopy(selection.new_tab)) {
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE,
        base::BindOnce(
            &TabOrganizationController::BringSelectedPage,
            weak_factory_.GetWeakPtr(),
            (selection.reason &
             TabStripModelObserver::CHANGE_REASON_USER_GESTURE) != 0));
  }
  // A tab that leaves for another window, as in a drag, takes its page along.
  bool moved_out = false;
  if (change.type() == TabStripModelChange::kRemoved) {
    for (const TabStripModelChange::RemovedTab& removed :
         change.GetRemove()->contents) {
      const std::optional<PageId> id = IdFor(removed.tab);
      if (id && removed.remove_reason ==
                    TabRemovedReason::kInsertedIntoOtherTabStrip) {
        // A drag may have moved and nested it on its way out, so it keeps
        // the place it had when the drag began.
        SharedTree::Depart(removed.tab->GetHandle(), *id, shared_.get(),
                           dragging_ ? drag_before_ : shared_->tree());
        moved_out = true;
      }
    }
  }
  std::set<PageId> moved;
  if (change.type() == TabStripModelChange::kMoved) {
    if (dragging_ && !drag_carried_.empty()) {
      moved = drag_carried_;
    } else if (const std::optional<PageId> id = IdFor(change.GetMove()->tab)) {
      moved.insert(*id);
    }
  }
  Mirror(moved);
  NotifyNativeMoves(moved);
  // A window opened for a tab closes once the tab moved on to another window,
  // or, during a drag, once the drag ends.
  if (moved_out && !dragging_) {
    CloseIfLeft();
  }
  if (selection.active_tab_changed()) {
    if (const std::optional<PageId> old = IdFor(selection.old_tab)) {
      previous_active_ = old;
    }
    active_ = IdFor(selection.new_tab);
    Remember(active_);
    // Picking another tab uses the window for more than the tab it was
    // opened for, and leaves its blank tab unused.
    if ((selection.reason &
         TabStripModelObserver::CHANGE_REASON_USER_GESTURE) &&
        active_ && !opened_for_.contains(*active_)) {
      opened_for_.clear();
    }
    if (unused_blank_ && active_ != unused_blank_) {
      base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
          FROM_HERE,
          base::BindOnce(&TabOrganizationController::CloseUnusedBlank,
                         weak_factory_.GetWeakPtr(), /*closing=*/false));
    }
  }
  // A tab strip may not change while it reports a change, so the window
  // joins its workspace, and takes in its tabs, right after.
  if (!joined_ && change.type() == TabStripModelChange::kInserted &&
      base::FeatureList::IsEnabled(kUwSharedTabs)) {
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(&TabOrganizationController::JoinWorkspace,
                                  weak_factory_.GetWeakPtr()));
  }
  ScheduleApply();
}

void TabOrganizationController::OnTabPinnedStateChanged(tabs::TabInterface*,
                                                        int) {
  if (applying_) {
    return;
  }
  base::AutoReset<bool> observing(&observing_, true);
  Mirror({});
  ScheduleApply();
}

void TabOrganizationController::OnTabGroupChanged(
    const TabGroupChange& change) {
  if (change.type != TabGroupChange::kVisualsChanged) {
    return;
  }
  if (joined_ && !applying_ &&
      tabs_->group_model()->ContainsTabGroup(change.group)) {
    shared_->SetVisuals(
        this, KeyOf(change.group),
        *tabs_->group_model()->GetTabGroup(change.group)->visual_data());
  }
  ScheduleUpdate();
}

void TabOrganizationController::OnTabChangedAt(tabs::TabInterface* tab,
                                               TabChangeType) {
  const std::optional<PageId> id = IdFor(tab);
  if (joined_ && !applying_ && id && !IsCopy(tab)) {
    shared_->ScheduleRefresh(*id);
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
  base::AutoReset<bool> observing(&observing_, true);
  std::set<PageId> moved;
  if (dragging_ && !drag_carried_.empty()) {
    moved = drag_carried_;
  } else if (const std::optional<PageId> id = IdFor(tab)) {
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
  base::AutoReset<bool> observing(&observing_, true);
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
    const std::vector<PageId> branch = shared_->tree().BranchOf(root);
    moved.insert(moved.end(), branch.begin(), branch.end());
  }
  manual_moves_.Notify(moved);
}

std::vector<PageId> TabOrganizationController::Roots(
    const std::vector<PageId>& ids) const {
  std::vector<PageId> roots;
  for (const Page& page : shared_->tree().pages()) {
    if (!Has(ids, page.id)) {
      continue;
    }
    bool covered = false;
    for (const Page* up = shared_->tree().Find(page.parent); up && !covered;
         up = shared_->tree().Find(up->parent)) {
      covered = Has(ids, up->id);
    }
    if (!covered) {
      roots.push_back(page.id);
    }
  }
  return roots;
}

std::vector<PageId> TabOrganizationController::Parents(
    const std::vector<PageId>& ids) const {
  std::vector<PageId> parents = Roots(ids);
  std::erase_if(parents, [this](const PageId& id) {
    return shared_->tree().BranchOf(id).size() < 2;
  });
  return parents;
}

void TabOrganizationController::RevealSelected() {
  const std::optional<PageId> selected = IdFor(tabs_->GetActiveTab());
  if (!selected || !IsHidden(*selected)) {
    return;
  }
  for (const Page* up =
           shared_->tree().Find(shared_->tree().Find(*selected)->parent);
       up; up = shared_->tree().Find(up->parent)) {
    shared_->collapsed().erase(up->id);
  }
}

void TabOrganizationController::LeaveBehind(
    const std::vector<PageId>& roots,
    const std::vector<PageId>& staying) {
  const auto inside = [&](const PageId& id) {
    return std::ranges::any_of(roots, [&](const PageId& root) {
      return root != id && Has(shared_->tree().BranchOf(root), id);
    });
  };
  for (const PageId& id : staying) {
    while (inside(id) && shared_->tree().Outdent(id)) {
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
    if (KeyOf(group) == task) {
      return group;
    }
  }
  return std::nullopt;
}

void TabOrganizationController::Mirror(const std::set<PageId>& moved) {
  // A closing window leaves first, before Chromium takes its tabs out of
  // their tasks and closes them, so it closes no tab in another window.
  if (joined_ && tabs_->closing_all()) {
    Leave();
  }
  // A tab that came from another window keeps its page, and this window's
  // copy of it makes way. A window that opened with such tabs was opened for
  // them.
  const bool opening = ids_.empty();
  for (int i = 0; i < tabs_->count(); ++i) {
    const tabs::TabInterface* tab = tabs_->GetTabAtIndex(i);
    if (tab->IsPinned() || ids_.contains(tab->GetHandle())) {
      continue;
    }
    std::optional<SharedTree::Transit> transit =
        SharedTree::Arrive(tab->GetHandle());
    if (!transit) {
      continue;
    }
    for (const auto& [handle, id] : ids_) {
      if (id == transit->page) {
        leaving_.insert(handle);
      }
    }
    ids_[tab->GetHandle()] = transit->page;
    if (opening) {
      opened_for_.insert(transit->page);
      opened_from_ = std::move(transit->before);
    }
  }
  if (!leaving_.empty()) {
    std::erase_if(ids_, [this](const auto& entry) {
      return leaving_.contains(entry.first);
    });
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(&TabOrganizationController::CloseLeaving,
                                  weak_factory_.GetWeakPtr()));
  }
  std::set<tabs::TabHandle> live;
  std::vector<Page> strip;
  for (int i = 0; i < tabs_->count(); ++i) {
    const tabs::TabInterface* tab = tabs_->GetTabAtIndex(i);
    if (tab->IsPinned() || leaving_.contains(tab->GetHandle())) {
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
  Drop drop{.collapsed = shared_->collapsed()};
  if (dragging_ && !drag_carried_.empty()) {
    drop.levels = drag_levels_;
    drop.before = &drag_before_;
  }
  shared_->tree().Adopt(strip, moved, drop);
  std::erase_if(shared_->opened(),
                [this](const PageId& id) { return !shared_->tree().Find(id); });
  LinkRestoredPages();
  RevealSelected();
  ScheduleUpdate();
  if (joined_) {
    shared_->Propagate(this);
    shared_->CloseIfEmpty();
  }
}

void TabOrganizationController::LinkRestoredPages() {
  for (auto it = restored_parents_.begin(); it != restored_parents_.end();) {
    const Page* child = shared_->tree().Find(it->first);
    const Page* parent = shared_->tree().Find(it->second);
    if (child && !parent) {
      ++it;
      continue;
    }
    if (child && child->task == parent->task) {
      shared_->tree().SetParent(it->first, it->second);
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
    base::WeakPtr<TabOrganizationController> self = weak_factory_.GetWeakPtr();
    ApplyCopies();
    // Closing a window's last tab closes the window.
    if (!self || tabs_->empty()) {
      return;
    }
    ApplyTasks();
    ApplyOrder();
  }
  Mirror({});
}

void TabOrganizationController::ApplyCopies() {
  if (!joined_) {
    return;
  }
  std::vector<int> gone;
  for (const auto& [handle, id] : ids_) {
    const tabs::TabInterface* tab = handle.Get();
    if (tab && !shared_->tree().Find(id)) {
      gone.push_back(tabs_->GetIndexOfTab(tab));
    }
  }
  std::ranges::sort(gone, std::greater<>());
  for (int index : gone) {
    tabs_->CloseWebContentsAt(index, TabCloseTypes::CLOSE_NONE);
  }
  for (const Page& page : shared_->tree().pages()) {
    if (TabFor(page.id)) {
      continue;
    }
    content::WebContents* source = shared_->PageOf(page.id, this);
    if (!source) {
      continue;
    }
    std::unique_ptr<content::WebContents> copy = SharedTree::MakeCopy(source);
    content::WebContents* added = copy.get();
    tabs_->InsertWebContentsAt(tabs_->count(), std::move(copy),
                               AddTabTypes::ADD_NONE);
    if (const tabs::TabInterface* tab = tabs_->GetTabForWebContents(added)) {
      ids_[tab->GetHandle()] = page.id;
    }
  }
}

void TabOrganizationController::ApplyTasks() {
  std::vector<int> ungroup;
  for (const Page& page : shared_->tree().pages()) {
    const tabs::TabInterface* tab = TabFor(page.id);
    if (tab && page.task.empty() && tab->GetGroup()) {
      ungroup.push_back(tabs_->GetIndexOfTab(tab));
    }
  }
  if (!ungroup.empty()) {
    std::ranges::sort(ungroup);
    tabs_->RemoveFromGroup(ungroup);
  }

  for (const std::string& task : shared_->tree().Tasks()) {
    std::vector<int> indices;
    for (const Page& page : shared_->tree().pages()) {
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
    if (joined_ && (pending == pending_tasks_.end() || !token)) {
      // A window following a task another window made makes a group of its
      // own, keyed to the task, with the look the other window gave it. Tab
      // group ids must differ between windows.
      const tab_groups::TabGroupId group = tabs_->AddToNewGroup(indices);
      keys_.insert_or_assign(group, task);
      if (const std::optional<tab_groups::TabGroupVisualData> visuals =
              shared_->VisualsOf(task, this)) {
        tabs_->ChangeTabGroupVisuals(group, *visuals, /*is_customized=*/true);
      }
      continue;
    }
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
  for (const std::string& task : shared_->tree().Tasks()) {
    const std::optional<tab_groups::TabGroupId> group = GroupFor(task);
    if (!group) {
      continue;
    }
    const TabGroup* native = tabs_->group_model()->GetTabGroup(*group);
    if (static_cast<int>(native->ListTabs().start()) != next) {
      tabs_->MoveGroupTo(*group, next);
    }
    for (const Page& page : shared_->tree().pages()) {
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
  for (const Page& page : shared_->tree().pages()) {
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
  // The other windows follow at once, so none of them reads its strip back
  // into the tree before it has caught up.
  if (joined_) {
    shared_->Propagate(this);
  }
  ScheduleOwnApply();
}

void TabOrganizationController::ScheduleOwnApply() {
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
  if (dragging_) {
    return;
  }
  Apply();
}

void TabOrganizationController::Record(const std::vector<PageId>& roots,
                                       std::optional<std::string> task) {
  SharedTree::Step snapshot;
  snapshot.tree = shared_->tree();
  snapshot.task = std::move(task);
  if (const tabs::TabInterface* tab =
          roots.empty() ? nullptr : TabFor(roots.front())) {
    snapshot.tab = tab->GetContents()->GetTitle();
  }
  if (tabs_->SupportsTabGroups()) {
    for (const tab_groups::TabGroupId& group :
         tabs_->group_model()->ListTabGroups()) {
      snapshot.tasks.emplace(
          KeyOf(group),
          *tabs_->group_model()->GetTabGroup(group)->visual_data());
    }
  }
  std::vector<SharedTree::Step>& undo = shared_->undo();
  undo.push_back(std::move(snapshot));
  if (undo.size() > kMaxUndoActions) {
    undo.erase(undo.begin());
  }
}

void TabOrganizationController::ScheduleUpdate() {
  if (joined_) {
    shared_->Touch();
    return;
  }
  ScheduleOwnUpdate();
}

void TabOrganizationController::ScheduleOwnUpdate() {
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
  if (tabs_->SupportsTabGroups()) {
    std::erase_if(keys_, [this](const auto& entry) {
      return !tabs_->group_model()->ContainsTabGroup(entry.first);
    });
  }
  Profile* profile = window_->GetProfile();
  SessionService* service = profile->IsOffTheRecord()
                                ? nullptr
                                : SessionServiceFactory::GetForProfile(profile);
  std::erase_if(shared_->generated_names(),
                [this](const auto& entry) { return !GroupFor(entry.first); });
  // A restored parent keeps its collapse until its children are linked.
  std::erase_if(shared_->collapsed(), [this](const PageId& id) {
    return shared_->tree().BranchOf(id).size() < 2 &&
           !std::ranges::any_of(
               restored_parents_,
               [&](const auto& link) { return link.second == id; });
  });
  // A kept tab the user moved to another task or to the Scratchpad is let go.
  std::erase_if(shared_->kept(),
                [this](const auto& entry) { return !IsKept(entry.first); });
  // A held tab the user moved, or whose parent closed, is let go. A restored
  // one waits until it is linked to its parent.
  std::erase_if(shared_->held(), [this](const auto& entry) {
    return !IsHeld(entry.first) && !restored_parents_.contains(entry.first);
  });
  std::map<PageId, std::string> states;
  for (const auto& [handle, id] : ids_) {
    const Page* page = shared_->tree().Find(id);
    const tabs::TabInterface* tab = handle.Get();
    content::WebContents* contents = tab ? tab->GetContents() : nullptr;
    if (!page || !contents) {
      continue;
    }
    base::DictValue state;
    state.Set("id", id.value());
    state.Set("parent", page->parent.value());
    if (shared_->collapsed().contains(id)) {
      state.Set("collapsed", true);
    }
    if (IsKept(id)) {
      state.Set("kept", true);
    }
    if (IsHeld(id)) {
      state.Set("held", true);
    }
    if (HasGeneratedName(page->task)) {
      state.Set("task_name", shared_->generated_names()[page->task]);
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
