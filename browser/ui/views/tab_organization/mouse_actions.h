// Copyright 2026 The uw Authors. All rights reserved.

#ifndef UW_BROWSER_UI_VIEWS_TAB_ORGANIZATION_MOUSE_ACTIONS_H_
#define UW_BROWSER_UI_VIEWS_TAB_ORGANIZATION_MOUSE_ACTIONS_H_

#include <string>

#include "base/callback_list.h"
#include "components/tab_groups/tab_group_id.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/views/controls/button/label_button.h"

class BrowserWindowInterface;

namespace tabs {
class TabInterface;
}

namespace ui {
class Event;
class MouseEvent;
}  // namespace ui

namespace uw {

// The tab list's mouse actions, per the keyboard and mouse plan. Each has a
// switch on uw's settings page. With it off, the click does what Chromium
// does.

// Option-click on the close button of `tab`, on `row`, closes the tab's
// branch. False when the click stays Chromium's, as for a tab with no tabs
// under it.
bool CloseBranchOnOptionClick(const views::View* row,
                              const tabs::TabInterface* tab,
                              const ui::Event& event);

// A middle-click on the row of `task` closes the task. `row` takes the press
// when TakesMiddleClick, and closes the task on the release over it.
bool TakesMiddleClick(const views::View* row, const ui::MouseEvent& event);
bool CloseTaskOnMiddleClick(views::View* row,
                            const tab_groups::TabGroupId& task,
                            const ui::MouseEvent& event);

// A double-click on the empty space below the wide sidebar's tabs does what
// the foot's "+" does. That space drags the window, and macOS acts on a
// double-click there, so while the second click is down or coming up, the
// space is the views' instead.
bool TakesDoubleClickBelowTabs(BrowserWindowInterface* window);
bool OpenTabOnDoubleClick(BrowserWindowInterface* window,
                          const ui::MouseEvent& event);

// Shows the "+" and the × on a task's `row` while its menu button shows, the
// "+" unless its switch is off.
void ShowTaskRowButtons(views::View* row, bool menu_shown);
// The fill of a task's `row`: the close color while a close of its task is
// hinted, otherwise none.
SkColor TaskRowFill(const views::View* row);

// The "+" on a task's row in the wide sidebar, just before the row's menu
// button and shown with it. It opens the command bar for the task, or a blank
// tab at the task's end while the command bar is off.
class TaskRowNewTabButton : public views::LabelButton {
  METADATA_HEADER(TaskRowNewTabButton, views::LabelButton)

 public:
  explicit TaskRowNewTabButton(const tab_groups::TabGroupId& task);
  TaskRowNewTabButton(const TaskRowNewTabButton&) = delete;
  TaskRowNewTabButton& operator=(const TaskRowNewTabButton&) = delete;
  ~TaskRowNewTabButton() override;

  // views::LabelButton:
  void OnThemeChanged() override;

 private:
  void Open();

  const tab_groups::TabGroupId task_;
};

// The × on a task's row in the wide sidebar, just after the row's menu button
// and shown with it. It closes the task and its tabs, as Close Task does.
// While the pointer is on it, the task's row and its tab rows take the close
// color. It has no switch. See
// docs/plans/keyboard-and-mouse.md#close-button-on-a-tasks-row.
class TaskRowCloseButton : public views::LabelButton {
  METADATA_HEADER(TaskRowCloseButton, views::LabelButton)

 public:
  explicit TaskRowCloseButton(const tab_groups::TabGroupId& task);
  TaskRowCloseButton(const TaskRowCloseButton&) = delete;
  TaskRowCloseButton& operator=(const TaskRowCloseButton&) = delete;
  ~TaskRowCloseButton() override;

  const tab_groups::TabGroupId& task() const { return task_; }

  // views::LabelButton:
  void AddedToWidget() override;
  void RemovedFromWidget() override;
  void VisibilityChanged(views::View* starting_from, bool is_visible) override;
  void OnThemeChanged() override;
  void StateChanged(ButtonState old_state) override;
  std::u16string GetRenderedTooltipText(const gfx::Point& point) const override;

 private:
  void Close();
  // Hints the close while the pointer is on the ×.
  void UpdateHint();
  // Fills the row for the hint.
  void UpdateRow();

  const tab_groups::TabGroupId task_;
  base::CallbackListSubscription hints_;
};

}  // namespace uw

#endif  // UW_BROWSER_UI_VIEWS_TAB_ORGANIZATION_MOUSE_ACTIONS_H_
