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
inline constexpr int kFullGrain = 100;
inline constexpr int kFullGlass = 100;

// Light, Dark, or Auto, which follows macOS.
enum class ThemeMode { kAuto, kLight, kDark };

// A theme, as the picker sets it: a workspace's, or a profile's.
struct Theme {
  // One to three colors, in the order they were added.
  std::vector<SkColor> colors = {kDefaultLightTint};
  // How much of each color's saturation shows, from 0 to 100.
  int intensity = kFullIntensity;
  // How strong the grain over the theme is, from 0 to 100.
  int grain = 0;
  // How much of the desktop shows through the window, from 0, solid, to 100,
  // the clearest.
  int glass = 0;
  // Dark mode shows the theme darkened instead of black.
  bool colors_in_dark_mode = false;
  ThemeMode mode = ThemeMode::kAuto;

  bool operator==(const Theme&) const = default;
};

// The picker's suggested themes, in order. The first is the default.
std::vector<std::vector<SkColor>> SuggestedThemes();

// `color` with its saturation scaled to `intensity` percent.
SkColor WithIntensity(SkColor color, int intensity);

// `color` as theme colors show in dark mode: the same hue and saturation at
// 14% lightness.
SkColor DarkModeColorOf(SkColor color);

// How much of the glass behind the window the theme covers at `glass`: all of
// it when solid, down to a fifth at the clearest.
float GlassCover(int glass);

// The glass past which text in `text` stops keeping its contrast over any
// desktop: where the theme covers 60% of the glass under near-black text, and
// 75% under white text.
int ReadableGlass(SkColor text);

// The least that `color` covers the glass so that `text` on it keeps its
// contrast over a white and a black desktop alike.
float ReadableCover(SkColor color, SkColor text);

// The colors after the first that the window's gradient runs through: each at
// the theme's intensity, darkened in dark mode, and made lighter or darker
// until `text` reads on it.
std::vector<SkColor> MoreGradientColors(const Theme& theme,
                                        bool dark,
                                        SkColor text);

// The colors the window's gradient runs through when `frame_color` comes from
// `theme`: the frame color, then the theme's more colors. Empty for a theme of
// one color, and when the frame color comes from elsewhere, such as black dark
// mode, Chromium's own themes, or a color picked in Chromium's color picker.
std::vector<SkColor> GradientColorsFor(const Theme& theme, SkColor frame_color);

}  // namespace uw

#endif  // UW_COMPONENTS_LOOK_THEME_H_
