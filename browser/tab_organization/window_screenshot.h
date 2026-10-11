// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#ifndef UW_BROWSER_TAB_ORGANIZATION_WINDOW_SCREENSHOT_H_
#define UW_BROWSER_TAB_ORGANIZATION_WINDOW_SCREENSHOT_H_

#include <string>

class Browser;

namespace uw {

// Saves the whole window of `browser` as <name>.png in the folder that the
// UW_SCREENSHOT_DIR environment variable names, so looks can be reviewed
// without opening a window. Does nothing when the variable is not set.
void SaveWindowScreenshot(Browser* browser, const std::string& name);

}  // namespace uw

#endif  // UW_BROWSER_TAB_ORGANIZATION_WINDOW_SCREENSHOT_H_
