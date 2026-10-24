// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/components/look/theme.h"

#include <algorithm>
#include <cmath>

#include "ui/gfx/color_utils.h"
#include "uw/components/look/tint.h"

namespace uw {
namespace {

constexpr SkColor kSky = SkColorSetRGB(0xD8, 0xE6, 0xF9);
constexpr SkColor kMint = SkColorSetRGB(0xCF, 0xEF, 0xDD);
constexpr SkColor kButter = SkColorSetRGB(0xF7, 0xE0, 0x8A);
constexpr SkColor kPeach = SkColorSetRGB(0xF9, 0xE3, 0xD2);
constexpr SkColor kWine = SkColorSetRGB(0x5A, 0x1F, 0x3A);
constexpr SkColor kForest = SkColorSetRGB(0x1F, 0x3A, 0x2E);

constexpr double kDarkModeLightness = 0.14;

// The theme covers the glass from all of it, solid, down to a fifth.
constexpr float kClearestCover = 0.2f;
// The least cover that keeps text readable over any desktop, as the designs
// mark it on the slider.
constexpr float kReadableCoverUnderDarkText = 0.6f;
constexpr float kReadableCoverUnderLightText = 0.75f;
constexpr int kCoverSteps = 100;
constexpr double kLightnessStep = 0.01;

// `color` made lighter under dark text, or darker under light text, until the
// text reads on it.
SkColor ReadableUnder(SkColor color, SkColor text) {
  color_utils::HSL hsl;
  color_utils::SkColorToHSL(color, &hsl);
  const double step =
      color_utils::IsDark(text) ? kLightnessStep : -kLightnessStep;
  while (color_utils::GetContrastRatio(color, text) < kReadableContrast &&
         hsl.l > 0 && hsl.l < 1) {
    hsl.l = std::clamp(hsl.l + step, 0.0, 1.0);
    color = color_utils::HSLToSkColor(hsl, SK_AlphaOPAQUE);
  }
  return color;
}

}  // namespace

std::vector<std::vector<SkColor>> SuggestedThemes() {
  return {{kDefaultLightTint},
          {kSky},
          {kMint},
          {kButter},
          {kPeach},
          {kWine},
          {kForest},
          {kDefaultLightTint, kSky},
          {kPeach, kButter},
          {kMint, kSky}};
}

SkColor WithIntensity(SkColor color, int intensity) {
  if (intensity >= kFullIntensity) {
    return SkColorSetA(color, SK_AlphaOPAQUE);
  }
  color_utils::HSL hsl;
  color_utils::SkColorToHSL(color, &hsl);
  hsl.s *= std::max(intensity, 0) / static_cast<double>(kFullIntensity);
  return color_utils::HSLToSkColor(hsl, SK_AlphaOPAQUE);
}

SkColor DarkModeColorOf(SkColor color) {
  color_utils::HSL hsl;
  color_utils::SkColorToHSL(color, &hsl);
  hsl.l = kDarkModeLightness;
  return color_utils::HSLToSkColor(hsl, SK_AlphaOPAQUE);
}

float GlassCover(int glass) {
  return 1.0f - (1.0f - kClearestCover) * std::clamp(glass, 0, kFullGlass) /
                    kFullGlass;
}

int ReadableGlass(SkColor text) {
  const float cover = color_utils::IsDark(text) ? kReadableCoverUnderDarkText
                                                : kReadableCoverUnderLightText;
  return static_cast<int>(
      std::lround((1.0f - cover) / (1.0f - kClearestCover) * kFullGlass));
}

float ReadableCover(SkColor color, SkColor text) {
  for (int step = 0; step < kCoverSteps; ++step) {
    const float cover = static_cast<float>(step) / kCoverSteps;
    if (color_utils::GetContrastRatio(
            color_utils::AlphaBlend(color, SK_ColorWHITE, cover), text) >=
            kReadableContrast &&
        color_utils::GetContrastRatio(
            color_utils::AlphaBlend(color, SK_ColorBLACK, cover), text) >=
            kReadableContrast) {
      return cover;
    }
  }
  return 1.0f;
}

std::vector<SkColor> MoreGradientColors(const Theme& theme,
                                        bool dark,
                                        SkColor text) {
  std::vector<SkColor> more;
  for (size_t i = 1; i < std::min(theme.colors.size(), kMaxThemeColors); ++i) {
    SkColor color = WithIntensity(theme.colors[i], theme.intensity);
    if (dark) {
      color = DarkModeColorOf(color);
    }
    more.push_back(ReadableUnder(color, text));
  }
  return more;
}

std::vector<SkColor> GradientColorsFor(const Theme& theme,
                                       SkColor frame_color) {
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

}  // namespace uw
