// Copyright 2026 The uw Authors. All rights reserved.

#ifndef UW_BROWSER_UI_VIEWS_TAB_ORGANIZATION_MOVE_TO_TASK_MENU_H_
#define UW_BROWSER_UI_VIEWS_TAB_ORGANIZATION_MOVE_TO_TASK_MENU_H_

#include "base/functional/callback_forward.h"
#include "components/tab_groups/tab_group_id.h"

class BrowserWindowInterface;

namespace views {
class View;
}

namespace uw {

// Move to Task… while the command bar is off: the selected tab's Move to Task
// menu, Chromium's, beside its row in the sidebar. Nothing shows while the
// row is hidden.
void ShowMoveToTaskMenu(BrowserWindowInterface* window);

// The workspaces the task `group` can move to, beside `row`. Picking one
// moves the task, then runs `picked`.
void ShowMoveTaskMenuBeside(BrowserWindowInterface* window,
                            const tab_groups::TabGroupId& group,
                            views::View* row,
                            base::OnceClosure picked);

// Move Task to Workspace while the command bar is off: the workspaces beside
// the selected tab's row, for its task. Nothing shows while the row is
// hidden.
void ShowMoveTaskMenu(BrowserWindowInterface* window);

}  // namespace uw

#endif  // UW_BROWSER_UI_VIEWS_TAB_ORGANIZATION_MOVE_TO_TASK_MENU_H_
