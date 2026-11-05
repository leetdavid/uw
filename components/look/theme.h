// Copyright 2026 The uw Authors. All rights reserved.

#ifndef UW_COMPONENTS_LOOK_THEME_H_
#define UW_COMPONENTS_LOOK_THEME_H_

#include <cstddef>
#include <vector>

#include "third_party/skia/include/core/SkColor.h"
#include "uw/components/look/tint.h"

namespace uw {

inline constexpr size_t kMaxThemeColors = 3;
inline constexpr int kFullIntensity = 100;
inline constexpr int kFullBrightness = 100;
inline constexpr int kFullGrain = 100;
inline constexpr int kFullTransparency = 100;
inline constexpr int kFullGlass = 100;

// Sky, the second color of the default theme, lilac to sky.
inline constexpr SkColor kSky = SkColorSetRGB(0xD8, 0xE6, 0xF9);

// A theme, as the picker sets it: a workspace's, or a profile's. It has no
// light or dark of its own: a theme whose text is white is dark.
struct Theme {
  // One to three colors, in the order they were added.
  std::vector<SkColor> colors = {kDefaultLightTint, kSky};
  // How much of each color's chroma shows, from 0 to 100.
  int intensity = kFullIntensity;
  // How much of each color's lightness shows, from 0, black, to 100.
  int brightness = kFullBrightness;
  // How strong the grain over the theme is, from 0 to 100.
  int grain = 0;
  // How much of the desktop shows through the window, from 0, solid, to 100,
  // the clearest.
  int transparency = 0;
  // How glassy the window's pin tiles, active tab, and buttons are, from 0,
  // flat, through frosted to 100, clear.
  int glass = 0;

  bool operator==(const Theme&) const = default;
};

// One of the picker's suggested themes.
struct SuggestedTheme {
  std::vector<SkColor> colors;
  int brightness = kFullBrightness;

  bool operator==(const SuggestedTheme&) const = default;
};

// The picker's suggested themes, in order. The first is the default.
std::vector<SuggestedTheme> SuggestedThemes();

// `color` with its OKLCH chroma scaled to `intensity` percent.
SkColor WithIntensity(SkColor color, int intensity);

// `color` with its OKLCH lightness scaled to `brightness` percent.
SkColor WithBrightness(SkColor color, int brightness);

// `color`, one of `theme`'s, at the theme's intensity and brightness.
SkColor ColorAsShown(const Theme& theme, SkColor color);

// True when `theme` takes white text, which makes it dark.
bool IsDarkTheme(const Theme& theme);

// True when `color` has almost no hue of its own, as black, white, and grays,
// so Chromium's colors made from it are neutral.
bool IsNeutral(SkColor color);

// How much of the glass behind the window the theme covers at `transparency`:
// all of it when solid, down to a fifth at the clearest.
float WindowCover(int transparency);

// The transparency past which text in `text` stops keeping its contrast over
// any desktop: where the theme covers 60% of the glass under near-black text,
// and 75% under white text.
int ReadableTransparency(SkColor text);

// How much of its frosted white a glass element keeps at `glass`: nearly all
// at the least glass, down to a seventh at the clearest.
float GlassTint(int glass);

// The least that `color` covers the glass so that `text` on it keeps its
// contrast over a white and a black desktop alike.
float ReadableCover(SkColor color, SkColor text);

// The colors after the first that the window's gradient runs through: each as
// shown, and made lighter or darker until `text` reads on it.
std::vector<SkColor> MoreGradientColors(const Theme& theme, SkColor text);

// The colors the window's gradient runs through when `frame_color` comes from
// `theme`: the frame color, then the theme's more colors. Empty for a theme of
// one color, and when the frame color comes from elsewhere, such as Chromium's
// own themes or a color picked in Chromium's color picker.
std::vector<SkColor> GradientColorsFor(const Theme& theme, SkColor frame_color);

// The picker's color field, each axis from 0 to 1: the hue runs left to right,
// pale at the top to full at `kFieldPaleUntil` and below.
inline constexpr float kFieldPaleUntil = 0.52f;

// A point of the color field.
struct FieldPoint {
  float x;
  float y;
};

// The color under `point`.
SkColor FieldColorAt(FieldPoint point);

// The point of the field whose color is nearest `color`.
FieldPoint FieldPointOf(SkColor color);

}  // namespace uw

#endif  // UW_COMPONENTS_LOOK_THEME_H_
