// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#ifndef UW_BROWSER_AGENT_RUN_WINDOW_MARK_H_
#define UW_BROWSER_AGENT_RUN_WINDOW_MARK_H_

class BrowserWindowInterface;

namespace uw {

// Whether `window` is a profile's run window, the browser window scheduled
// runs open their tabs in. It is never shown, so Chromium never takes it for
// the window the user used last, and uw gives it none of its per-window
// features. Chromium's files ask through this target, which depends on
// nothing but //base.
bool IsRunWindow(const BrowserWindowInterface* window);

// Whether the browser window being made right now is a run window. Its
// features are made before the window is marked.
bool IsMakingRunWindow();

class ScopedMakingRunWindow {
 public:
  ScopedMakingRunWindow();
  ScopedMakingRunWindow(const ScopedMakingRunWindow&) = delete;
  ScopedMakingRunWindow& operator=(const ScopedMakingRunWindow&) = delete;
  ~ScopedMakingRunWindow();
};

// Marks `window` as a run window, or unmarks it as it closes.
void MarkRunWindow(const BrowserWindowInterface* window, bool run_window);

}  // namespace uw

#endif  // UW_BROWSER_AGENT_RUN_WINDOW_MARK_H_
