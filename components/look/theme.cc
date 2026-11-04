// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/components/look/theme.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>

#include "ui/gfx/color_utils.h"
#include "uw/components/look/tint.h"

namespace uw {
namespace {

constexpr SkColor kMint = SkColorSetRGB(0xCF, 0xEF, 0xDD);
constexpr SkColor kButter = SkColorSetRGB(0xF7, 0xE0, 0x8A);
constexpr SkColor kPeach = SkColorSetRGB(0xF9, 0xE3, 0xD2);

// Wine is a rose at 43% brightness, and forest a pale green at 37%.
constexpr FieldPoint kRose = {355.0f / 360.0f, kFieldPaleUntil};
constexpr int kWineBrightness = 43;
constexpr FieldPoint kPaleGreen = {164.0f / 360.0f, 0.19f};
constexpr int kForestBrightness = 37;

// Below this OKLCH chroma a color has no hue to speak of.
constexpr float kNeutralChroma = 0.01f;

// The color field's full colors, and how pale its top is.
constexpr float kFieldLightness = 0.78f;
constexpr float kFieldChroma = 0.11f;
constexpr float kPalest = 0.92f;
constexpr int kFieldColumns = 360;
constexpr int kFieldRows = 52;

// The theme covers the glass from all of it, solid, down to a fifth.
constexpr float kClearestCover = 0.2f;
// A glass element keeps from all of its frosted white down to this much.
constexpr float kClearestTint = 0.15f;
// The least cover that keeps text readable over any desktop, as the designs
// mark it on the slider.
constexpr float kReadableCoverUnderDarkText = 0.6f;
constexpr float kReadableCoverUnderLightText = 0.75f;
constexpr int kCoverSteps = 100;
constexpr float kLightnessStep = 0.01f;

// `color` made lighter under dark text, or darker under light text, until the
// text reads on it.
SkColor ReadableUnder(SkColor color, SkColor text) {
  Oklch oklch = OklchOf(color);
  const float step =
      color_utils::IsDark(text) ? kLightnessStep : -kLightnessStep;
  while (color_utils::GetContrastRatio(color, text) < kReadableContrast &&
         oklch.l > 0 && oklch.l < 1) {
    oklch.l = std::clamp(oklch.l + step, 0.0f, 1.0f);
    color = ColorOf(oklch);
  }
  return color;
}

// The field's full color at `x`.
SkColor FullFieldColorAt(float x) {
  return ColorOf(
      {kFieldLightness, kFieldChroma, std::clamp(x, 0.0f, 1.0f) * 360});
}

// `full` as pale as the field is at `y`.
SkColor PaleAt(SkColor full, float y) {
  const float paleness =
      kPalest *
      std::max(0.0f, 1.0f - std::clamp(y, 0.0f, 1.0f) / kFieldPaleUntil);
  return color_utils::AlphaBlend(SK_ColorWHITE, full, paleness);
}

// How far apart two colors look, in OKLab.
float Distance(const Oklch& a, const Oklch& b) {
  const float to_radians = std::numbers::pi_v<float> / 180.0f;
  const float da = a.c * std::cos(a.h * to_radians) -
                   b.c * std::cos(b.h * to_radians);
  const float db = a.c * std::sin(a.h * to_radians) -
                   b.c * std::sin(b.h * to_radians);
  return std::hypot(a.l - b.l, da, db);
}

}  // namespace

std::vector<SuggestedTheme> SuggestedThemes() {
  return {{{kDefaultLightTint, kSky}},
          {{kDefaultLightTint}},
          {{kSky}},
          {{kMint}},
          {{kButter}},
          {{kPeach}},
          {{FieldColorAt(kRose)}, kWineBrightness},
          {{FieldColorAt(kPaleGreen)}, kForestBrightness},
          {{kDefaultLightTint}, 0},
          {{kPeach, kButter}},
          {{kMint, kSky}}};
}

SkColor WithIntensity(SkColor color, int intensity) {
  if (intensity >= kFullIntensity) {
    return SkColorSetA(color, SK_AlphaOPAQUE);
  }
  Oklch oklch = OklchOf(color);
  oklch.c *= std::max(intensity, 0) / static_cast<float>(kFullIntensity);
  return ColorOf(oklch);
}

SkColor WithBrightness(SkColor color, int brightness) {
  if (brightness >= kFullBrightness) {
    return SkColorSetA(color, SK_AlphaOPAQUE);
  }
  Oklch oklch = OklchOf(color);
  oklch.l *= std::max(brightness, 0) / static_cast<float>(kFullBrightness);
  return ColorOf(oklch);
}

SkColor ColorAsShown(const Theme& theme, SkColor color) {
  return WithBrightness(WithIntensity(color, theme.intensity),
                        theme.brightness);
}

bool IsDarkTheme(const Theme& theme) {
  return !theme.colors.empty() &&
         IsDark(TintFrom(ColorAsShown(theme, theme.colors[0])));
}

bool IsNeutral(SkColor color) {
  return OklchOf(color).c < kNeutralChroma;
}

float WindowCover(int transparency) {
  return 1.0f - (1.0f - kClearestCover) *
                    std::clamp(transparency, 0, kFullTransparency) /
                    kFullTransparency;
}

int ReadableTransparency(SkColor text) {
  const float cover = color_utils::IsDark(text) ? kReadableCoverUnderDarkText
                                                : kReadableCoverUnderLightText;
  return static_cast<int>(std::lround((1.0f - cover) / (1.0f - kClearestCover) *
                                      kFullTransparency));
}

float GlassTint(int glass) {
  return 1.0f -
         (1.0f - kClearestTint) * std::clamp(glass, 0, kFullGlass) / kFullGlass;
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

std::vector<SkColor> MoreGradientColors(const Theme& theme, SkColor text) {
  std::vector<SkColor> more;
  for (size_t i = 1; i < std::min(theme.colors.size(), kMaxThemeColors); ++i) {
    more.push_back(ReadableUnder(ColorAsShown(theme, theme.colors[i]), text));
  }
  return more;
}

std::vector<SkColor> GradientColorsFor(const Theme& theme,
                                       SkColor frame_color) {
  if (theme.colors.size() < 2) {
    return {};
  }
  const Tint tint = TintFrom(ColorAsShown(theme, theme.colors[0]));
  if (tint.start != frame_color) {
    return {};
  }
  std::vector<SkColor> colors = MoreGradientColors(theme, TextColorOn(tint));
  colors.insert(colors.begin(), tint.start);
  return colors;
}

SkColor FieldColorAt(FieldPoint point) {
  return PaleAt(FullFieldColorAt(point.x), point.y);
}

FieldPoint FieldPointOf(SkColor color) {
  const Oklch target = OklchOf(color);
  FieldPoint nearest = {0, 0};
  float least = std::numeric_limits<float>::max();
  for (int column = 0; column < kFieldColumns; ++column) {
    const float x = static_cast<float>(column) / kFieldColumns;
    const SkColor full = FullFieldColorAt(x);
    for (int row = 0; row <= kFieldRows; ++row) {
      const float y = kFieldPaleUntil * static_cast<float>(row) / kFieldRows;
      const float distance = Distance(OklchOf(PaleAt(full, y)), target);
      if (distance < least) {
        least = distance;
        nearest = {x, y};
      }
    }
  }
  return nearest;
}

}  // namespace uw
