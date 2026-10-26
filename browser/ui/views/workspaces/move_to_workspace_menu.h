// Copyright 2026 The uw Authors. All rights reserved.

#ifndef UW_BROWSER_UI_VIEWS_WORKSPACES_MOVE_TO_WORKSPACE_MENU_H_
#define UW_BROWSER_UI_VIEWS_WORKSPACES_MOVE_TO_WORKSPACE_MENU_H_

namespace tabs {
class TabInterface;
}

namespace ui {
class SimpleMenuModel;
}

namespace uw {

// The Move to Workspace row of a tab's menu, which opens its submenu.
inline constexpr int kMoveToWorkspaceCommand = 56104;

bool IsMoveToWorkspaceCommand(int command);

// Adds Move to Workspace after the moves into tasks in `tab`'s menu, when
// there is another workspace. Its submenu lists the profile's other
// workspaces first and then, under "Reopens in another profile", the rest.
void AddMoveToWorkspace(ui::SimpleMenuModel* menu,
                        const tabs::TabInterface* tab);

}  // namespace uw

#endif  // UW_BROWSER_UI_VIEWS_WORKSPACES_MOVE_TO_WORKSPACE_MENU_H_
