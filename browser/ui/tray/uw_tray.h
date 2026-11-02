// Copyright 2026 The uw Authors. All rights reserved.

#ifndef UW_BROWSER_UI_TRAY_UW_TRAY_H_
#define UW_BROWSER_UI_TRAY_UW_TRAY_H_

namespace uw {

// Shows uw's icon in Windows' notification area while uw runs. Clicking it
// brings a window to the front, or opens one, and its menu opens a new window
// or quits uw. Once is enough; later calls do nothing.
void StartTray();

}  // namespace uw

#endif  // UW_BROWSER_UI_TRAY_UW_TRAY_H_
