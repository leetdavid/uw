// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/components/look/tint.h"

#include <algorithm>

#include "ui/gfx/color_utils.h"

namespace uw {
namespace {

constexpr double kHueTurn = -40.0 / 360.0;
constexpr float kActiveTabOnDark = 0.16f;
constexpr double kLightnessStep = 0.01;
constexpr int kLightnessSteps = 100;

Tint NeighborsOf(SkColor start) {
  color_utils::HSL hsl;
  color_utils::SkColorToHSL(start, &hsl);
  hsl.h += kHueTurn;
  if (hsl.h < 0) {
    hsl.h += 1;
  }
  return {SkColorSetA(start, SK_AlphaOPAQUE),
          color_utils::HSLToSkColor(hsl, SK_AlphaOPAQUE)};
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

Tint TintFrom(SkColor picked) {
  const Tint exact = NeighborsOf(picked);
  if (IsReadable(exact)) {
    return exact;
  }
  color_utils::HSL hsl;
  color_utils::SkColorToHSL(picked, &hsl);
  for (int step = 1; step <= kLightnessSteps; ++step) {
    for (double direction : {-1.0, 1.0}) {
      color_utils::HSL nudged = hsl;
      nudged.l =
          std::clamp(hsl.l + direction * step * kLightnessStep, 0.0, 1.0);
      const Tint tint =
          NeighborsOf(color_utils::HSLToSkColor(nudged, SK_AlphaOPAQUE));
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
