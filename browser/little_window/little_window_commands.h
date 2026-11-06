// Copyright 2026 The uw Authors. All rights reserved.

#ifndef UW_BROWSER_LITTLE_WINDOW_LITTLE_WINDOW_COMMANDS_H_
#define UW_BROWSER_LITTLE_WINDOW_LITTLE_WINDOW_COMMANDS_H_

namespace uw {

// File → New Little Window, the new button's menu, and Option+Cmd+N.
inline constexpr int kNewLittleWindowCommand = 56105;
// In a little window, the menu of workspaces its page can move to, on
// Cmd+Shift+O.
inline constexpr int kChooseWorkspaceCommand = 56106;
// Open Link in Little Window, in a link's menu.
inline constexpr int kOpenLinkInLittleWindowCommand = 56107;

}  // namespace uw

#endif  // UW_BROWSER_LITTLE_WINDOW_LITTLE_WINDOW_COMMANDS_H_
