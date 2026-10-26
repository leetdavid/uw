// Copyright 2026 The uw Authors. All rights reserved.

#ifndef UW_BROWSER_UI_GLASS_GLASS_PREFS_H_
#define UW_BROWSER_UI_GLASS_GLASS_PREFS_H_

class PrefRegistrySimple;

namespace uw {

// "Glass in windows behind the front one" on uw's settings page, for all of
// uw. Off, only the front window shows glass.
inline constexpr char kGlassBehindPref[] = "uw.glass.windows_behind";

void RegisterGlassLocalStatePrefs(PrefRegistrySimple* registry);

}  // namespace uw

#endif  // UW_BROWSER_UI_GLASS_GLASS_PREFS_H_
