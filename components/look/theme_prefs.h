// Copyright 2026 The uw Authors. All rights reserved.

#ifndef UW_COMPONENTS_LOOK_THEME_PREFS_H_
#define UW_COMPONENTS_LOOK_THEME_PREFS_H_

#include <optional>
#include <vector>

#include "base/values.h"
#include "third_party/skia/include/core/SkColor.h"
#include "uw/components/look/theme.h"

class PrefRegistrySimple;
class PrefService;

namespace uw {

// A profile's theme, kept in its prefs. Its first color also lives in
// Chromium's user color, at the theme's intensity, so Chromium's color picker
// and its colors stay in step with the theme.
inline constexpr char kThemeColorsPref[] = "uw.theme.colors";
inline constexpr char kThemeIntensityPref[] = "uw.theme.intensity";
inline constexpr char kThemeGrainPref[] = "uw.theme.grain";
inline constexpr char kThemeTransparencyPref[] = "uw.theme.transparency";
inline constexpr char kThemeGlassPref[] = "uw.theme.glass";
inline constexpr char kThemeColorsInDarkModePref[] =
    "uw.theme.colors_in_dark_mode";
inline constexpr char kThemePageModePref[] = "uw.theme.page_mode";

void RegisterThemeProfilePrefs(PrefRegistrySimple* registry);

// The theme saved in `prefs`, given Chromium's `user_color`. With no user
// color, it is the default theme. When the saved first color at the saved
// intensity is not the user color, the color was picked elsewhere, such as in
// Chromium's color picker or before themes, and the theme is that one color.
Theme ReadTheme(const PrefService& prefs, std::optional<SkColor> user_color);

// `theme` as a workspace keeps it, and back. Nothing for a value without
// colors.
base::DictValue ThemeToValue(const Theme& theme);
std::optional<Theme> ThemeFromValue(const base::DictValue& value);

// Saves `theme`. The caller sets Chromium's user color to its first color at
// its intensity.
void WriteTheme(PrefService& prefs, const Theme& theme);

}  // namespace uw

#endif  // UW_COMPONENTS_LOOK_THEME_PREFS_H_
