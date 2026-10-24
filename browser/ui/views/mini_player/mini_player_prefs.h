// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#ifndef UW_BROWSER_UI_VIEWS_MINI_PLAYER_MINI_PLAYER_PREFS_H_
#define UW_BROWSER_UI_VIEWS_MINI_PLAYER_MINI_PLAYER_PREFS_H_

class PrefRegistrySimple;

namespace uw {

// The mini player's switches on uw's settings page, for all of uw, per the
// "Settings" section of docs/plans/mini-player.md. All are on by default.

// "Show the mini player in the sidebar". Off, it always floats.
inline constexpr char kMiniPlayerInSidebarPref[] = "uw.mini_player.in_sidebar";

// "Open the mini player when you switch tabs".
inline constexpr char kMiniPlayerOnTabSwitchPref[] =
    "uw.mini_player.on_tab_switch";

// "Float the mini player when uw is in the background".
inline constexpr char kMiniPlayerFloatInBackgroundPref[] =
    "uw.mini_player.float_in_background";

void RegisterMiniPlayerLocalStatePrefs(PrefRegistrySimple* registry);

}  // namespace uw

#endif  // UW_BROWSER_UI_VIEWS_MINI_PLAYER_MINI_PLAYER_PREFS_H_
