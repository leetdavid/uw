// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/components/look/theme_prefs.h"

#include "base/values.h"
#include "components/prefs/pref_registry_simple.h"
#include "components/prefs/pref_service.h"

namespace uw {

void RegisterThemeProfilePrefs(PrefRegistrySimple* registry) {
  registry->RegisterListPref(kThemeColorsPref);
  registry->RegisterIntegerPref(kThemeIntensityPref, kFullIntensity);
  registry->RegisterIntegerPref(kThemeGrainPref, 0);
  registry->RegisterBooleanPref(kThemeColorsInDarkModePref, false);
}

namespace {

// The theme as saved, with no saved colors as none.
Theme SavedTheme(const PrefService& prefs) {
  Theme theme;
  theme.colors.clear();
  for (const base::Value& color : prefs.GetList(kThemeColorsPref)) {
    if (color.is_int() && theme.colors.size() < kMaxThemeColors) {
      theme.colors.push_back(static_cast<SkColor>(color.GetInt()));
    }
  }
  theme.intensity = prefs.GetInteger(kThemeIntensityPref);
  theme.grain = prefs.GetInteger(kThemeGrainPref);
  theme.colors_in_dark_mode = prefs.GetBoolean(kThemeColorsInDarkModePref);
  return theme;
}

}  // namespace

Theme ReadTheme(const PrefService& prefs, std::optional<SkColor> user_color) {
  Theme theme = SavedTheme(prefs);
  if (!user_color) {
    theme.colors = Theme().colors;
    theme.intensity = kFullIntensity;
  } else if (theme.colors.empty() ||
             WithIntensity(theme.colors[0], theme.intensity) != *user_color) {
    theme.colors = {*user_color};
    theme.intensity = kFullIntensity;
  }
  return theme;
}

std::vector<SkColor> GradientColorsFor(const PrefService& prefs,
                                       SkColor frame_color) {
  const Theme theme = SavedTheme(prefs);
  if (theme.colors.size() < 2 || frame_color == SK_ColorBLACK) {
    return {};
  }
  const SkColor first = WithIntensity(theme.colors[0], theme.intensity);
  for (bool dark : {false, true}) {
    const Tint tint = TintFrom(dark ? DarkModeColorOf(first) : first);
    if (tint.start != frame_color || (dark && !theme.colors_in_dark_mode)) {
      continue;
    }
    std::vector<SkColor> colors =
        MoreGradientColors(theme, dark, TextColorOn(tint));
    colors.insert(colors.begin(), tint.start);
    return colors;
  }
  return {};
}

void WriteTheme(PrefService& prefs, const Theme& theme) {
  base::ListValue colors;
  for (SkColor color : theme.colors) {
    colors.Append(static_cast<int>(color));
  }
  prefs.SetList(kThemeColorsPref, std::move(colors));
  prefs.SetInteger(kThemeIntensityPref, theme.intensity);
  prefs.SetInteger(kThemeGrainPref, theme.grain);
  prefs.SetBoolean(kThemeColorsInDarkModePref, theme.colors_in_dark_mode);
}

}  // namespace uw
