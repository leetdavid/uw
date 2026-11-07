// Copyright 2026 The uw Authors. All rights reserved.

#ifndef UW_BROWSER_UI_VIEWS_TAB_ORGANIZATION_COMMAND_BAR_COMMANDS_H_
#define UW_BROWSER_UI_VIEWS_TAB_ORGANIZATION_COMMAND_BAR_COMMANDS_H_

#include <vector>

#include "uw/browser/ui/views/command_bar/command_bar.h"

class BrowserWindowInterface;

namespace uw {

// The commands the command bar finds, uw's and Chromium's common ones, that
// can run now in `window`, each with its key. See
// docs/plans/command-bar.md#what-it-finds.
std::vector<CommandBar::Command> ListCommandBarCommands(
    BrowserWindowInterface* window);

}  // namespace uw

#endif  // UW_BROWSER_UI_VIEWS_TAB_ORGANIZATION_COMMAND_BAR_COMMANDS_H_
