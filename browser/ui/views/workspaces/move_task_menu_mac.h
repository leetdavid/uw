// Copyright 2026 The uw Authors. All rights reserved.

#ifndef UW_BROWSER_UI_VIEWS_WORKSPACES_MOVE_TASK_MENU_MAC_H_
#define UW_BROWSER_UI_VIEWS_WORKSPACES_MOVE_TASK_MENU_MAC_H_

@class NSMenuItem;

namespace uw {

// Makes `item`, Tasks → Move Task to Workspace in the menu bar, list the
// workspaces the selected tab's task can move to each time it opens. It is
// dimmed while the selected tab is in the Scratchpad or there is no other
// workspace.
void SetUpMoveTaskMenu(NSMenuItem* item);

}  // namespace uw

#endif  // UW_BROWSER_UI_VIEWS_WORKSPACES_MOVE_TASK_MENU_MAC_H_
