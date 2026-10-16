// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#ifndef UW_BROWSER_TERMINAL_TERMINAL_TAB_H_
#define UW_BROWSER_TERMINAL_TERMINAL_TAB_H_

#include <string>

class BrowserWindowInterface;
class GURL;

namespace base {
class FilePath;
}

namespace content {
struct DropData;
class WebContents;
}  // namespace content

namespace uw {

// A uw command number, above every IDC_ value and above the commands of
// TabOrganizationController.
inline constexpr int kNewTerminalCommand = 56100;

// The page of a terminal tab is chrome://terminal, shown as uw://terminal.
inline constexpr char kTerminalHost[] = "terminal";

// Opens a terminal tab where a blank new tab would open.
void OpenTerminalTab(BrowserWindowInterface* window);

// Opens a terminal tab the same way, and runs `command` in its shell once the
// shell starts.
void OpenTerminalTabRunning(BrowserWindowInterface* window,
                            const std::string& command);

// The command a terminal tab opened by OpenTerminalTabRunning runs, taken
// once. Empty for any other tab.
std::string TakeStartCommand(content::WebContents* contents);

bool IsTerminalUrl(const GURL& url);

// The folder the shell of a terminal tab is in. Empty until the shell reports
// it, and for any other tab.
base::FilePath TerminalFolder(content::WebContents* contents);
void SetTerminalFolder(content::WebContents* contents,
                       const base::FilePath& folder);

// Files dropped on a terminal tab reach its page as their paths, quoted for
// the shell, and the page pastes them. A drop on any other tab is left alone.
void DropFilesAsPaths(content::WebContents* contents, content::DropData& drop);

}  // namespace uw

#endif  // UW_BROWSER_TERMINAL_TERMINAL_TAB_H_
