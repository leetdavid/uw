// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/browser/ui/views/tab_organization/move_to_task_menu.h"

#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/memory/raw_ptr.h"
#include "base/no_destructor.h"
#include "base/task/sequenced_task_runner.h"
#include "chrome/browser/ui/browser_window/public/browser_window_features.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/tabs/existing_tab_group_sub_menu_model.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/tabs/common/tab_view.h"
#include "components/tabs/public/tab_interface.h"
#include "ui/base/mojom/menu_source_type.mojom.h"
#include "ui/menus/simple_menu_model.h"
#include "ui/views/controls/menu/menu_runner.h"
#include "ui/views/view.h"
#include "ui/views/view_utils.h"
#include "uw/browser/tab_organization/tab_organization_controller.h"
#include "uw/browser/workspaces/workspace_move_menu.h"

namespace uw {
namespace {

// The row of `tab` in `view`, or null.
TabView* RowOf(views::View* view, const tabs::TabInterface* tab) {
  if (auto* row = views::AsViewClass<TabView>(view);
      row && row->GetTabInterface() == tab) {
    return row;
  }
  for (views::View* child : view->children()) {
    if (TabView* row = RowOf(child, tab)) {
      return row;
    }
  }
  return nullptr;
}

// The menu, whose New Task runs as the tab menu's does.
class MoveToTaskMenu : public ui::SimpleMenuModel::Delegate {
 public:
  MoveToTaskMenu(BrowserWindowInterface* window, int index)
      : model_(window->GetTabStripModel()),
        index_(index),
        menu_(this,
              window->GetFeatures().tab_menu_model_delegate(),
              model_,
              index) {}

  void RunBeside(views::View* row, base::RepeatingClosure closed) {
    runner_ = std::make_unique<views::MenuRunner>(
        &menu_, views::MenuRunner::HAS_MNEMONICS, std::move(closed));
    runner_->RunMenuAt(row->GetWidget(), nullptr, row->GetBoundsInScreen(),
                       views::MenuAnchorPosition::kBubbleRight,
                       ui::mojom::MenuSourceType::kKeyboard);
  }

  // ui::SimpleMenuModel::Delegate:
  void ExecuteCommand(int command_id, int event_flags) override {
    model_->ExecuteContextMenuCommand(
        index_, static_cast<TabStripModel::ContextMenuCommand>(command_id));
  }

 private:
  const raw_ptr<TabStripModel> model_;
  const int index_;
  ExistingTabGroupSubMenuModel menu_;
  std::unique_ptr<views::MenuRunner> runner_;
};

std::unique_ptr<MoveToTaskMenu>& Shown() {
  static base::NoDestructor<std::unique_ptr<MoveToTaskMenu>> menu;
  return *menu;
}

// The menu closes from inside itself, so it is deleted afterwards.
void OnClosed() {
  base::SequencedTaskRunner::GetCurrentDefault()->DeleteSoon(
      FROM_HERE, std::move(Shown()));
}

// The workspaces a task can move to. A card the row is in may close as the
// menu opens, so the menu lives apart from it until it closes.
class MoveTaskMenu {
 public:
  MoveTaskMenu(BrowserWindowInterface* window,
               const tab_groups::TabGroupId& group,
               base::OnceClosure picked)
      : window_(window->GetWeakPtr()),
        group_(group),
        picked_(std::move(picked)),
        menu_(
            window,
            base::BindRepeating(&MoveTaskMenu::Move, base::Unretained(this))) {}

  void RunBeside(views::View* row, base::RepeatingClosure closed) {
    runner_ = std::make_unique<views::MenuRunner>(
        &menu_, views::MenuRunner::HAS_MNEMONICS, std::move(closed));
    runner_->RunMenuAt(row->GetWidget(), nullptr, row->GetBoundsInScreen(),
                       views::MenuAnchorPosition::kBubbleRight,
                       ui::mojom::MenuSourceType::kMouse);
  }

 private:
  void Move(const std::string& workspace) {
    if (window_) {
      TabOrganizationController::MoveTaskToWorkspace(window_.get(), group_,
                                                     workspace);
    }
    if (picked_) {
      std::move(picked_).Run();
    }
  }

  const base::WeakPtr<BrowserWindowInterface> window_;
  const tab_groups::TabGroupId group_;
  base::OnceClosure picked_;
  WorkspaceMoveMenu menu_;
  std::unique_ptr<views::MenuRunner> runner_;
};

std::unique_ptr<MoveTaskMenu>& ShownTaskMenu() {
  static base::NoDestructor<std::unique_ptr<MoveTaskMenu>> menu;
  return *menu;
}

void OnTaskMenuClosed() {
  base::SequencedTaskRunner::GetCurrentDefault()->DeleteSoon(
      FROM_HERE, std::move(ShownTaskMenu()));
}

}  // namespace

void ShowMoveToTaskMenu(BrowserWindowInterface* window) {
  TabStripModel* model = window->GetTabStripModel();
  const tabs::TabInterface* tab = model->GetActiveTab();
  TabView* row =
      tab ? RowOf(BrowserView::GetBrowserViewForBrowser(window), tab) : nullptr;
  if (Shown() || !row || !row->IsDrawn() || !model->SupportsTabGroups()) {
    return;
  }
  Shown() = std::make_unique<MoveToTaskMenu>(window, model->active_index());
  Shown()->RunBeside(row, base::BindRepeating(&OnClosed));
}

void ShowMoveTaskMenuBeside(BrowserWindowInterface* window,
                            const tab_groups::TabGroupId& group,
                            views::View* row,
                            base::OnceClosure picked) {
  if (ShownTaskMenu()) {
    return;
  }
  ShownTaskMenu() =
      std::make_unique<MoveTaskMenu>(window, group, std::move(picked));
  ShownTaskMenu()->RunBeside(row, base::BindRepeating(&OnTaskMenuClosed));
}

void ShowMoveTaskMenu(BrowserWindowInterface* window) {
  const tabs::TabInterface* tab = window->GetTabStripModel()->GetActiveTab();
  const std::optional<tab_groups::TabGroupId> group =
      tab ? tab->GetGroup() : std::nullopt;
  TabView* row =
      tab ? RowOf(BrowserView::GetBrowserViewForBrowser(window), tab) : nullptr;
  if (!group || !row || !row->IsDrawn()) {
    return;
  }
  ShowMoveTaskMenuBeside(window, *group, row, base::DoNothing());
}

}  // namespace uw
