// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#ifndef UW_BROWSER_TERMINAL_TERMINAL_TAB_H_
#define UW_BROWSER_TERMINAL_TERMINAL_TAB_H_

class BrowserWindowInterface;
class GURL;

namespace base {
class FilePath;
}

namespace content {
class WebContents;
}

namespace uw {

// A uw command number, above every IDC_ value and above the commands of
// TabOrganizationController.
inline constexpr int kNewTerminalCommand = 56100;

// The page of a terminal tab is chrome://terminal, shown as uw://terminal.
inline constexpr char kTerminalHost[] = "terminal";

// Opens a terminal tab where a blank new tab would open.
void OpenTerminalTab(BrowserWindowInterface* window);

bool IsTerminalUrl(const GURL& url);

// The folder the shell of a terminal tab is in. Empty until the shell reports
// it, and for any other tab.
base::FilePath TerminalFolder(content::WebContents* contents);
void SetTerminalFolder(content::WebContents* contents,
                       const base::FilePath& folder);

}  // namespace uw

#endif  // UW_BROWSER_TERMINAL_TERMINAL_TAB_H_
