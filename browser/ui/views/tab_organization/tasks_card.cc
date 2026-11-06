// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/browser/ui/views/tab_organization/tasks_card.h"

#include <optional>
#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "base/memory/weak_ptr.h"
#include "base/scoped_observation.h"
#include "base/task/sequenced_task_runner.h"
#include "chrome/app/chrome_command_ids.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/tabs/tab_group_model.h"
#include "chrome/browser/ui/tabs/tab_group_theme.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/tab_strip_region_view.h"
#include "chrome/grit/generated_resources.h"
#include "components/tab_groups/tab_group_visual_data.h"
#include "components/tabs/public/tab_group.h"
#include "components/vector_icons/vector_icons.h"
#include "ui/base/accelerators/accelerator.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/views/bubble/bubble_dialog_delegate_view.h"
#include "ui/views/controls/button/menu_button_controller.h"
#include "ui/views/widget/widget.h"
#include "ui/views/widget/widget_observer.h"
#include "uw/browser/tab_organization/tab_organization_controller.h"
#include "uw/browser/ui/vector_icons/vector_icons.h"
#include "uw/browser/ui/views/tab_organization/menu_card_row.h"
#include "uw/browser/ui/views/tab_organization/window_commands.h"

namespace uw {
namespace {

// The bubble of the Tasks card. The caller keeps the widget, so the bubble
// deletes itself once the widget is gone. While the card is open, Tasks stays
// pressed and a floating sidebar stays out.
class TasksCardBubble : public views::BubbleDialogDelegate,
                        public views::WidgetObserver {
 public:
  TasksCardBubble(BrowserWindowInterface* window,
                  views::View* button,
                  views::MenuButtonController* controller)
      : BubbleDialogDelegate(button, views::BubbleBorder::TOP_LEFT),
        pressed_(controller ? controller->TakeLock() : nullptr),
        hover_lock_(BrowserView::GetBrowserViewForBrowser(window)
                        ->tab_strip_view()
                        ->GetExpandOnHoverLock(
                            ExpandOnHoverLockType::kKeepCurrentState)) {}

  void Watch(views::Widget* widget) { observation_.Observe(widget); }

  // views::WidgetObserver:
  void OnWidgetDestroyed(views::Widget* widget) override {
    observation_.Reset();
    pressed_.reset();
    hover_lock_.reset();
    // The button's window outlives the card, and would pass its theme changes
    // on to the gone card until this deletes.
    SetAnchorView(nullptr);
    base::SequencedTaskRunner::GetCurrentDefault()->DeleteSoon(FROM_HERE, this);
  }

 private:
  std::unique_ptr<views::MenuButtonController::PressedLock> pressed_;
  std::unique_ptr<ExpandOnHoverLock> hover_lock_;
  base::ScopedObservation<views::Widget, views::WidgetObserver> observation_{
      this};
};

}  // namespace

// static
std::unique_ptr<views::Widget> TasksCard::Show(
    BrowserWindowInterface* window,
    views::View* button,
    views::MenuButtonController* controller) {
  auto* bubble = new TasksCardBubble(window, button, controller);
  SetUpMenuCard(bubble);
  bubble->set_highlight_button_when_shown(false);
  bubble->set_adjust_if_offscreen(true);
  bubble->SetContentsView(std::make_unique<TasksCard>(window));
  std::unique_ptr<views::Widget> widget =
      views::BubbleDialogDelegate::CreateBubble(bubble);
  bubble->Watch(widget.get());
  widget->Show();
  return widget;
}

TasksCard::TasksCard(BrowserWindowInterface* window) : window_(window) {
  LayOutMenuCard(this);
  auto new_task = std::make_unique<MenuCardRow>(
      base::BindRepeating(&TasksCard::NewTask, base::Unretained(this)),
      kFolderPlusIcon, l10n_util::GetStringUTF16(IDS_UW_NEW_TASK_MAC));
  new_task->SetID(kNewTaskId);
  ui::Accelerator accelerator;
  if (GetCommandAccelerator(window, IDC_CREATE_NEW_TAB_GROUP, &accelerator)) {
    new_task->SetEnd(MakeMenuCardKey(accelerator));
  }
  AddChildView(std::move(new_task));

  if (CanRunWindowCommand(window, TabOrganizationController::kNewTask)) {
    auto move = std::make_unique<MenuCardRow>(
        base::BindRepeating(&TasksCard::MoveToNewTask, base::Unretained(this)),
        kFolderInputIcon,
        l10n_util::GetStringUTF16(IDS_UW_MOVE_TO_NEW_TASK_MAC));
    move->SetID(kMoveToNewTaskId);
    if (GetCommandAccelerator(window, TabOrganizationController::kNewTask,
                              &accelerator)) {
      move->SetEnd(MakeMenuCardKey(accelerator));
    }
    AddChildView(std::move(move));
  }

  const TabOrganizationController* controller =
      TabOrganizationController::From(window);
  if (!controller) {
    return;
  }
  const TabGroupModel* groups = window->GetTabStripModel()->group_model();
  std::vector<std::unique_ptr<MenuCardRow>> rows;
  for (const std::string& task : controller->tree().Tasks()) {
    const std::optional<tab_groups::TabGroupId> group =
        controller->GroupFor(task);
    if (!group || !groups->ContainsTabGroup(*group)) {
      continue;
    }
    auto row = std::make_unique<MenuCardRow>(
        base::BindRepeating(&TasksCard::GoToTask, base::Unretained(this), task),
        vector_icons::kFolderFilledIcon, controller->TaskTitle(task));
    row->SetIconColor(GetTabGroupTabStripColorId(
        groups->GetTabGroup(*group)->visual_data()->color(),
        /*active_frame=*/true));
    row->SetID(kFirstTaskId + static_cast<int>(rows.size()));
    rows.push_back(std::move(row));
  }
  if (rows.empty()) {
    return;
  }
  AddChildView(MakeMenuCardSeparator());
  for (std::unique_ptr<MenuCardRow>& row : rows) {
    AddChildView(std::move(row));
  }
}

TasksCard::~TasksCard() = default;

void TasksCard::NewTask() {
  BrowserWindowInterface* window = window_;
  GetWidget()->Close();
  chrome::ExecuteCommand(window, IDC_CREATE_NEW_TAB_GROUP);
}

void TasksCard::MoveToNewTask() {
  BrowserWindowInterface* window = window_;
  GetWidget()->Close();
  chrome::ExecuteCommand(window, TabOrganizationController::kNewTask);
}

// A collapsed task opens, then the tab used last in it is selected.
void TasksCard::GoToTask(const std::string& task) {
  BrowserWindowInterface* window = window_;
  GetWidget()->Close();
  TabOrganizationController* controller =
      TabOrganizationController::From(window);
  if (!controller) {
    return;
  }
  TabStripModel* tabs = window->GetTabStripModel();
  const std::optional<tab_groups::TabGroupId> group =
      controller->GroupFor(task);
  if (group && tabs->IsGroupCollapsed(*group)) {
    const tab_groups::TabGroupVisualData* visuals =
        tabs->group_model()->GetTabGroup(*group)->visual_data();
    tabs->ChangeTabGroupVisuals(*group, tab_groups::TabGroupVisualData(
                                            visuals->title(), visuals->color(),
                                            /*is_collapsed=*/false));
  }
  controller->SelectTask(task);
}

BEGIN_METADATA(TasksCard)
END_METADATA

}  // namespace uw
