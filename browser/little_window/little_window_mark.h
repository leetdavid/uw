// Copyright 2026 The uw Authors. All rights reserved.

#ifndef UW_BROWSER_LITTLE_WINDOW_LITTLE_WINDOW_MARK_H_
#define UW_BROWSER_LITTLE_WINDOW_LITTLE_WINDOW_MARK_H_

#include <string>

class BrowserWindowInterface;

namespace uw {

// Saved with a closed little window, its workspace, so Cmd+Shift+T brings it
// back as a little window.
inline constexpr char kLittleWindowRestoreKey[] = "uw.little_window";

// Whether `window` is a little window: a Chromium pop-up window that uw draws
// like a regular window without the sidebar, per ADR 0033. Chromium's own
// files ask through this target, which depends on nothing but //base.
bool IsLittleWindow(const BrowserWindowInterface* window);

// The workspace a little window belongs to, or an empty one for any other
// window.
std::string LittleWindowWorkspace(const BrowserWindowInterface* window);

// Whether the browser window being made right now is a little window. Its
// features are made before the window is marked.
bool IsMakingLittleWindow();

// The workspace of the little window being made right now.
const std::string& MakingLittleWindowWorkspace();

class ScopedMakingLittleWindow {
 public:
  explicit ScopedMakingLittleWindow(std::string workspace);
  ScopedMakingLittleWindow(const ScopedMakingLittleWindow&) = delete;
  ScopedMakingLittleWindow& operator=(const ScopedMakingLittleWindow&) =
      delete;
  ~ScopedMakingLittleWindow();
};

// Marks `window` as a little window of `workspace`, or unmarks it as it
// closes.
void MarkLittleWindow(const BrowserWindowInterface* window,
                      std::string workspace);
void UnmarkLittleWindow(const BrowserWindowInterface* window);

// Marks one of a little window's parts that Chromium makes before the window
// itself is marked, such as its WindowFeatureController, so the part can ask
// later. Each part unmarks itself as it goes.
void MarkLittleWindowPart(const void* part);
void UnmarkLittleWindowPart(const void* part);
bool IsLittleWindowPart(const void* part);

}  // namespace uw

#endif  // UW_BROWSER_LITTLE_WINDOW_LITTLE_WINDOW_MARK_H_
