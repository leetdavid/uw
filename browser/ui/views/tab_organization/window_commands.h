// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#ifndef UW_BROWSER_UI_VIEWS_TAB_ORGANIZATION_WINDOW_COMMANDS_H_
#define UW_BROWSER_UI_VIEWS_TAB_ORGANIZATION_WINDOW_COMMANDS_H_

class BrowserWindowInterface;

namespace ui {
class Accelerator;
}

namespace uw {

// Runs the commands IsWindowCommand accepts. The menu bar, the keys, and the
// Organize menu all come through here.
bool CanRunWindowCommand(BrowserWindowInterface* window, int command);
void RunWindowCommand(BrowserWindowInterface* window, int command);
bool IsWindowCommandChecked(BrowserWindowInterface* window, int command);
// The key that runs `command`, for menus to show beside it.
bool GetCommandAccelerator(BrowserWindowInterface* window,
                           int command,
                           ui::Accelerator* accelerator);

}  // namespace uw

#endif  // UW_BROWSER_UI_VIEWS_TAB_ORGANIZATION_WINDOW_COMMANDS_H_
