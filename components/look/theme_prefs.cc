// Copyright 2026 The uw Authors. All rights reserved.

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
  registry->RegisterIntegerPref(kThemeBrightnessPref, kFullBrightness);
  registry->RegisterIntegerPref(kThemeGrainPref, 0);
  registry->RegisterIntegerPref(kThemeTransparencyPref, 0);
  registry->RegisterIntegerPref(kThemeGlassPref, 0);
  registry->RegisterStringPref(kThemePageModePref, "window");
}

namespace {

// Theme is saved as "window", its name before T201.
std::string_view PageModeName(PageMode mode) {
  switch (mode) {
    case PageMode::kTheme:
      return "window";
    case PageMode::kLight:
      return "light";
    case PageMode::kDark:
      return "dark";
  }
}

PageMode PageModeNamed(std::string_view name) {
  return name == "light"  ? PageMode::kLight
         : name == "dark" ? PageMode::kDark
                          : PageMode::kTheme;
}

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
  theme.brightness = prefs.GetInteger(kThemeBrightnessPref);
  theme.grain = prefs.GetInteger(kThemeGrainPref);
  theme.transparency = prefs.GetInteger(kThemeTransparencyPref);
  theme.glass = prefs.GetInteger(kThemeGlassPref);
  theme.page_mode = PageModeNamed(prefs.GetString(kThemePageModePref));
  return theme;
}

}  // namespace

Theme ReadTheme(const PrefService& prefs, std::optional<SkColor> user_color) {
  Theme theme = SavedTheme(prefs);
  if (!user_color) {
    theme.colors = Theme().colors;
    theme.intensity = kFullIntensity;
    theme.brightness = kFullBrightness;
  } else if (theme.colors.empty() ||
             ColorAsShown(theme, theme.colors[0]) != *user_color) {
    theme.colors = {*user_color};
    theme.intensity = kFullIntensity;
    theme.brightness = kFullBrightness;
  }
  return theme;
}

base::DictValue ThemeToValue(const Theme& theme) {
  base::ListValue colors;
  for (SkColor color : theme.colors) {
    colors.Append(static_cast<int>(color));
  }
  return base::DictValue()
      .Set("colors", std::move(colors))
      .Set("intensity", theme.intensity)
      .Set("brightness", theme.brightness)
      .Set("grain", theme.grain)
      .Set("transparency", theme.transparency)
      .Set("glass", theme.glass)
      .Set("page_mode", PageModeName(theme.page_mode));
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
  theme.brightness = value.FindInt("brightness").value_or(kFullBrightness);
  theme.grain = value.FindInt("grain").value_or(0);
  theme.transparency = value.FindInt("transparency").value_or(0);
  theme.glass = value.FindInt("glass").value_or(0);
  if (const std::string* page_mode = value.FindString("page_mode")) {
    theme.page_mode = PageModeNamed(*page_mode);
  }
  return theme;
}

void WriteTheme(PrefService& prefs, const Theme& theme) {
  base::ListValue colors;
  for (SkColor color : theme.colors) {
    colors.Append(static_cast<int>(color));
  }
  prefs.SetList(kThemeColorsPref, std::move(colors));
  prefs.SetInteger(kThemeIntensityPref, theme.intensity);
  prefs.SetInteger(kThemeBrightnessPref, theme.brightness);
  prefs.SetInteger(kThemeGrainPref, theme.grain);
  prefs.SetInteger(kThemeTransparencyPref, theme.transparency);
  prefs.SetInteger(kThemeGlassPref, theme.glass);
  prefs.SetString(kThemePageModePref, PageModeName(theme.page_mode));
}

}  // namespace uw
