// Copyright 2026 The uw Authors. All rights reserved.

#ifndef UW_BROWSER_UI_VIEWS_TAB_ORGANIZATION_WINDOW_COMMANDS_H_
#define UW_BROWSER_UI_VIEWS_TAB_ORGANIZATION_WINDOW_COMMANDS_H_

#include <optional>
#include <string>

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
// The menu bar title of `command` when it depends on the selected tabs.
std::optional<std::u16string> GetWindowCommandTitle(
    BrowserWindowInterface* window,
    int command);
// Opens the AI providers page, where the user picks a model.
void ShowAiProviders(BrowserWindowInterface* window);
// The key that runs `command`, for menus to show beside it.
bool GetCommandAccelerator(BrowserWindowInterface* window,
                           int command,
                           ui::Accelerator* accelerator);

}  // namespace uw

#endif  // UW_BROWSER_UI_VIEWS_TAB_ORGANIZATION_WINDOW_COMMANDS_H_
