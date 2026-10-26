// Copyright 2026 The uw Authors. All rights reserved.

#ifndef UW_BROWSER_UI_GLASS_GLASS_SCREENSHOT_H_
#define UW_BROWSER_UI_GLASS_GLASS_SCREENSHOT_H_

#include <string>

#include "ui/gfx/native_ui_types.h"

class Browser;

namespace uw {

// Saves the window of `browser` with the menus and popups open over it, as
// macOS shows them, as <name>.png where SaveBitmap() saves. Their windows are
// children of the browser's, and macOS offers no picture of one alone.
void SaveWindowWithPopups(Browser* browser, const std::string& name);

// Puts a macOS desktop picture behind the window of `browser` and a little
// around it, then saves the window over it as macOS shows them, the same way.
// Only this process's windows are in the shot, so nothing else on the screen
// shows.
void SaveWindowOverDesktop(Browser* browser, const std::string& name);

// Does the same for any of this process's windows, such as the floating mini
// player.
void SaveNativeWindowOverDesktop(gfx::NativeWindow native_window,
                                 const std::string& name);

}  // namespace uw

#endif  // UW_BROWSER_UI_GLASS_GLASS_SCREENSHOT_H_
