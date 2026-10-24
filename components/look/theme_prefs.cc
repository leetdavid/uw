// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/components/look/theme_prefs.h"

#include <string>
#include <string_view>
#include <utility>

#include "base/values.h"
#include "components/prefs/pref_registry_simple.h"
#include "components/prefs/pref_service.h"

namespace uw {

void RegisterThemeProfilePrefs(PrefRegistrySimple* registry) {
  registry->RegisterListPref(kThemeColorsPref);
  registry->RegisterIntegerPref(kThemeIntensityPref, kFullIntensity);
  registry->RegisterIntegerPref(kThemeGrainPref, 0);
  registry->RegisterIntegerPref(kThemeTransparencyPref, 0);
  registry->RegisterIntegerPref(kThemeGlassPref, 0);
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
  theme.transparency = prefs.GetInteger(kThemeTransparencyPref);
  theme.glass = prefs.GetInteger(kThemeGlassPref);
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

base::DictValue ThemeToValue(const Theme& theme) {
  base::ListValue colors;
  for (SkColor color : theme.colors) {
    colors.Append(static_cast<int>(color));
  }
  const std::string_view mode = theme.mode == ThemeMode::kLight  ? "light"
                                : theme.mode == ThemeMode::kDark ? "dark"
                                                                 : "auto";
  return base::DictValue()
      .Set("colors", std::move(colors))
      .Set("intensity", theme.intensity)
      .Set("grain", theme.grain)
      .Set("transparency", theme.transparency)
      .Set("glass", theme.glass)
      .Set("colors_in_dark_mode", theme.colors_in_dark_mode)
      .Set("mode", mode);
}

std::optional<Theme> ThemeFromValue(const base::DictValue& value) {
  Theme theme;
  theme.colors.clear();
  if (const base::ListValue* colors = value.FindList("colors")) {
    for (const base::Value& color : *colors) {
      if (color.is_int() && theme.colors.size() < kMaxThemeColors) {
        theme.colors.push_back(static_cast<SkColor>(color.GetInt()));
      }
    }
  }
  if (theme.colors.empty()) {
    return std::nullopt;
  }
  theme.intensity = value.FindInt("intensity").value_or(kFullIntensity);
  theme.grain = value.FindInt("grain").value_or(0);
  theme.transparency = value.FindInt("transparency").value_or(0);
  theme.glass = value.FindInt("glass").value_or(0);
  theme.colors_in_dark_mode =
      value.FindBool("colors_in_dark_mode").value_or(false);
  const std::string* mode = value.FindString("mode");
  theme.mode = !mode              ? ThemeMode::kAuto
               : *mode == "light" ? ThemeMode::kLight
               : *mode == "dark"  ? ThemeMode::kDark
                                  : ThemeMode::kAuto;
  return theme;
}

void WriteTheme(PrefService& prefs, const Theme& theme) {
  base::ListValue colors;
  for (SkColor color : theme.colors) {
    colors.Append(static_cast<int>(color));
  }
  prefs.SetList(kThemeColorsPref, std::move(colors));
  prefs.SetInteger(kThemeIntensityPref, theme.intensity);
  prefs.SetInteger(kThemeGrainPref, theme.grain);
  prefs.SetInteger(kThemeTransparencyPref, theme.transparency);
  prefs.SetInteger(kThemeGlassPref, theme.glass);
  prefs.SetBoolean(kThemeColorsInDarkModePref, theme.colors_in_dark_mode);
}

}  // namespace uw
