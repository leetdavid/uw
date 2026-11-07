// Copyright 2026 The uw Authors. All rights reserved.

#ifndef UW_BROWSER_UI_VIEWS_TAB_ORGANIZATION_MOVE_TO_TASK_MENU_H_
#define UW_BROWSER_UI_VIEWS_TAB_ORGANIZATION_MOVE_TO_TASK_MENU_H_

class BrowserWindowInterface;

namespace uw {

// Move to Task… while the command bar is off: the selected tab's Move to Task
// menu, Chromium's, beside its row in the sidebar. Nothing shows while the
// row is hidden.
void ShowMoveToTaskMenu(BrowserWindowInterface* window);

}  // namespace uw

#endif  // UW_BROWSER_UI_VIEWS_TAB_ORGANIZATION_MOVE_TO_TASK_MENU_H_
