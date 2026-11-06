// Copyright 2026 The uw Authors. All rights reserved.

#ifndef UW_BROWSER_LITTLE_WINDOW_LITTLE_WINDOW_PREFS_H_
#define UW_BROWSER_LITTLE_WINDOW_LITTLE_WINDOW_PREFS_H_

class PrefRegistrySimple;

namespace uw {

// The Little windows settings, for all of uw, in local state. See
// docs/plans/little-window.md#settings.

// Whether the little window's key opens one while uw is in front.
inline constexpr char kLittleWindowKeyInUwPref[] = "uw.little_window.key_in_uw";
// Whether it opens one from any other app while uw runs.
inline constexpr char kLittleWindowKeyFromAppsPref[] =
    "uw.little_window.key_from_apps";
// Whether links from other apps open in little windows instead of at the top
// of the Scratchpad.
inline constexpr char kLittleWindowLinksPref[] =
    "uw.little_window.links_from_apps";
// How long a little window stays open while it is not the front window, in
// minutes. Zero keeps it open.
inline constexpr char kLittleWindowCloseAfterPref[] =
    "uw.little_window.close_after_minutes";
inline constexpr int kLittleWindowCloseAfterDefault = 6 * 60;

void RegisterLittleWindowLocalStatePrefs(PrefRegistrySimple* registry);

}  // namespace uw

#endif  // UW_BROWSER_LITTLE_WINDOW_LITTLE_WINDOW_PREFS_H_
