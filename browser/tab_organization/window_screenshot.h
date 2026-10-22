// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#ifndef UW_BROWSER_TAB_ORGANIZATION_WINDOW_SCREENSHOT_H_
#define UW_BROWSER_TAB_ORGANIZATION_WINDOW_SCREENSHOT_H_

#include <string>

#include "third_party/skia/include/core/SkBitmap.h"

class Browser;

namespace views {
class Widget;
}

namespace uw {

// The whole window of `browser` at 1280 by 800, as it is drawn once its
// animations have finished.
SkBitmap CaptureWindow(Browser* browser);

// Saves the whole window of `browser` as <name>.png in the folder that the
// UW_SCREENSHOT_DIR environment variable names, so looks can be reviewed
// without opening a window. Does nothing when the variable is not set.
void SaveWindowScreenshot(Browser* browser, const std::string& name);

// Saves `widget`, such as an open menu, the same way.
void SaveWidgetScreenshot(views::Widget* widget, const std::string& name);

// Saves the window of `browser` as macOS shows it, with the close, minimize,
// and zoom buttons. Only a real window has them.
void SaveNativeWindowScreenshot(Browser* browser, const std::string& name);

}  // namespace uw

#endif  // UW_BROWSER_TAB_ORGANIZATION_WINDOW_SCREENSHOT_H_
