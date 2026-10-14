// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#ifndef UW_BROWSER_TERMINAL_TERMINAL_TAB_H_
#define UW_BROWSER_TERMINAL_TERMINAL_TAB_H_

class BrowserWindowInterface;
class GURL;

namespace uw {

// A uw command number, above every IDC_ value and above the commands of
// TabOrganizationController.
inline constexpr int kNewTerminalCommand = 56100;

// The page of a terminal tab is chrome://terminal, shown as uw://terminal.
inline constexpr char kTerminalHost[] = "terminal";

// Opens a terminal tab where a blank new tab would open.
void OpenTerminalTab(BrowserWindowInterface* window);

bool IsTerminalUrl(const GURL& url);

}  // namespace uw

#endif  // UW_BROWSER_TERMINAL_TERMINAL_TAB_H_
