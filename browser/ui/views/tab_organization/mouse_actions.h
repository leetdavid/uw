// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#ifndef UW_BROWSER_UI_VIEWS_TAB_ORGANIZATION_MOUSE_ACTIONS_H_
#define UW_BROWSER_UI_VIEWS_TAB_ORGANIZATION_MOUSE_ACTIONS_H_

#include "components/tab_groups/tab_group_id.h"
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

  // Shows the "+" on `row` while its menu button shows, unless the switch is
  // off or the sidebar is narrow.
  static void ShowWithMenu(views::View* row, bool menu_shown);

  // views::LabelButton:
  void OnThemeChanged() override;

 private:
  void Open();

  const tab_groups::TabGroupId task_;
};

}  // namespace uw

#endif  // UW_BROWSER_UI_VIEWS_TAB_ORGANIZATION_MOUSE_ACTIONS_H_
