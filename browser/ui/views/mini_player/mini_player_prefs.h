// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#ifndef UW_BROWSER_UI_VIEWS_MINI_PLAYER_MINI_PLAYER_PREFS_H_
#define UW_BROWSER_UI_VIEWS_MINI_PLAYER_MINI_PLAYER_PREFS_H_

class PrefRegistrySimple;
class PrefService;

namespace uw {

// The mini player's settings on uw's settings page, for all of uw, per the
// "Settings" section of docs/plans/mini-player.md.

// "Picture in picture": where it goes. Stored as its number.
enum class MiniPlayerMode {
  // Docked in the sidebar, the default.
  kDock = 0,
  // Never docked. A video stays in its page when the user leaves its tab, and
  // one the user puts in picture in picture floats.
  kDontDock = 1,
  // No picture in picture at all.
  kOff = 2,
  kMaxValue = kOff,
};
inline constexpr char kMiniPlayerModePref[] = "uw.mini_player.mode";

// "Open the mini player when you switch tabs". On by default.
inline constexpr char kMiniPlayerOnTabSwitchPref[] =
    "uw.mini_player.on_tab_switch";

// "Float the mini player when uw is in the background". On by default.
inline constexpr char kMiniPlayerFloatInBackgroundPref[] =
    "uw.mini_player.float_in_background";

void RegisterMiniPlayerLocalStatePrefs(PrefRegistrySimple* registry);

// The mode `local_state` holds, or Dock without one.
MiniPlayerMode MiniPlayerModeOf(const PrefService* local_state);

}  // namespace uw

#endif  // UW_BROWSER_UI_VIEWS_MINI_PLAYER_MINI_PLAYER_PREFS_H_
