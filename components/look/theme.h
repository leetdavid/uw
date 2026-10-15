// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#ifndef UW_COMPONENTS_LOOK_THEME_H_
#define UW_COMPONENTS_LOOK_THEME_H_

#include <cstddef>
#include <vector>

#include "third_party/skia/include/core/SkColor.h"
#include "uw/components/look/tint.h"

namespace uw {

inline constexpr size_t kMaxThemeColors = 3;
inline constexpr int kFullIntensity = 100;

// A profile's theme, as the picker sets it.
struct Theme {
  // One to three colors, in the order they were added.
  std::vector<SkColor> colors = {kDefaultLightTint};
  // How much of each color's saturation shows, from 0 to 100.
  int intensity = kFullIntensity;
  // How strong the grain over the theme is, from 0 to 100.
  int grain = 0;
  // Dark mode shows the theme darkened instead of black.
  bool colors_in_dark_mode = false;

  bool operator==(const Theme&) const = default;
};

// The picker's suggested themes, in order. The first is the default.
std::vector<std::vector<SkColor>> SuggestedThemes();

// `color` with its saturation scaled to `intensity` percent.
SkColor WithIntensity(SkColor color, int intensity);

// `color` as theme colors show in dark mode: the same hue and saturation at
// 14% lightness.
SkColor DarkModeColorOf(SkColor color);

// The colors after the first that the window's gradient runs through: each at
// the theme's intensity, darkened in dark mode, and made lighter or darker
// until `text` reads on it.
std::vector<SkColor> MoreGradientColors(const Theme& theme,
                                        bool dark,
                                        SkColor text);

}  // namespace uw

#endif  // UW_COMPONENTS_LOOK_THEME_H_
