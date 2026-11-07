// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/browser/ui/views/tab_organization/move_to_task_menu.h"

#include <memory>
#include <utility>

#include "base/functional/bind.h"
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

}  // namespace uw
