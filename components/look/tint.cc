// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/components/look/tint.h"

#include <algorithm>
#include <cmath>
#include <tuple>

#include "ui/gfx/color_conversions.h"
#include "ui/gfx/color_utils.h"

namespace uw {
namespace {

constexpr float kHueTurn = -40.0f;
constexpr float kFullTurn = 360.0f;
constexpr float kActiveTabOnDark = 0.12f;
constexpr float kLightnessStep = 0.01f;
constexpr int kLightnessSteps = 100;
// A channel this far outside 0 to 1 still fits, as rounding error.
constexpr float kFitSlack = 0.0005f;
constexpr int kFitSteps = 24;

uint8_t Channel(float value) {
  return static_cast<uint8_t>(std::lround(std::clamp(value, 0.0f, 1.0f) * 255));
}

// The sRGB channels of `oklch`, which may fall outside 0 to 1.
std::tuple<float, float, float> ChannelsOf(float l, float c, float h) {
  const auto [lab_l, a, b] = gfx::LchToLab(l, c, h);
  const auto [x, y, z] = gfx::OklabToXYZD50(lab_l, a, b);
  return gfx::XYZD50ToSRGB(x, y, z);
}

bool Fits(const std::tuple<float, float, float>& channels) {
  const auto inside = [](float channel) {
    return channel >= -kFitSlack && channel <= 1 + kFitSlack;
  };
  const auto [r, g, b] = channels;
  return inside(r) && inside(g) && inside(b);
}

Tint NeighborsOf(SkColor start) {
  Oklch turned = OklchOf(start);
  turned.h = std::fmod(turned.h + kHueTurn + kFullTurn, kFullTurn);
  return {SkColorSetA(start, SK_AlphaOPAQUE), ColorOf(turned)};
}

// The lowest contrast `text` has anywhere on the tint, which is at one of
// its two ends.
float ContrastOn(const Tint& tint, SkColor text) {
  return std::min(color_utils::GetContrastRatio(tint.start, text),
                  color_utils::GetContrastRatio(tint.end, text));
}

bool IsReadable(const Tint& tint) {
  return ContrastOn(tint, TextColorOn(tint)) >= kReadableContrast;
}

}  // namespace

Oklch OklchOf(SkColor color) {
  const auto [x, y, z] = gfx::SRGBToXYZD50(SkColorGetR(color) / 255.0f,
                                           SkColorGetG(color) / 255.0f,
                                           SkColorGetB(color) / 255.0f);
  const auto [l, a, b] = gfx::XYZD50ToOklab(x, y, z);
  const auto [lightness, chroma, hue] = gfx::LabToLch(l, a, b);
  return {lightness, chroma, std::fmod(hue + kFullTurn, kFullTurn)};
}

SkColor ColorOf(const Oklch& oklch) {
  const float l = std::clamp(oklch.l, 0.0f, 1.0f);
  float chroma = std::max(oklch.c, 0.0f);
  if (!Fits(ChannelsOf(l, chroma, oklch.h))) {
    float fits = 0;
    for (int step = 0; step < kFitSteps; ++step) {
      const float middle = (fits + chroma) / 2;
      if (Fits(ChannelsOf(l, middle, oklch.h))) {
        fits = middle;
      } else {
        chroma = middle;
      }
    }
    chroma = fits;
  }
  const auto [r, g, b] = ChannelsOf(l, chroma, oklch.h);
  return SkColorSetRGB(Channel(r), Channel(g), Channel(b));
}

Tint TintFrom(SkColor picked) {
  const Tint exact = NeighborsOf(picked);
  if (IsReadable(exact)) {
    return exact;
  }
  const Oklch oklch = OklchOf(picked);
  for (int step = 1; step <= kLightnessSteps; ++step) {
    for (float direction : {-1.0f, 1.0f}) {
      Oklch nudged = oklch;
      nudged.l = std::clamp(oklch.l + direction * step * kLightnessStep, 0.0f,
                            1.0f);
      const Tint tint = NeighborsOf(ColorOf(nudged));
      if (IsReadable(tint)) {
        return tint;
      }
    }
  }
  return exact;
}

SkColor MiddleOf(const Tint& tint) {
  return color_utils::AlphaBlend(tint.start, tint.end, 0.5f);
}

SkColor TextColorOn(const Tint& tint) {
  return ContrastOn(tint, kDarkTextOnTint) >= ContrastOn(tint, kLightTextOnTint)
             ? kDarkTextOnTint
             : kLightTextOnTint;
}

bool IsDark(const Tint& tint) {
  return TextColorOn(tint) == kLightTextOnTint;
}

SkColor ActiveTabOn(const Tint& tint) {
  return IsDark(tint) ? color_utils::AlphaBlend(SK_ColorWHITE, MiddleOf(tint),
                                                kActiveTabOnDark)
                      : SK_ColorWHITE;
}

}  // namespace uw
