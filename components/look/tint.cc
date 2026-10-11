// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/components/look/tint.h"

#include "ui/gfx/color_utils.h"

namespace uw {
namespace {

constexpr double kHueTurn = -40.0 / 360.0;
constexpr float kFillOnLight = 0.46f;
constexpr float kFillOnDark = 0.08f;
constexpr float kActiveTabOnDark = 0.16f;

}  // namespace

Tint TintFrom(SkColor picked) {
  color_utils::HSL hsl;
  color_utils::SkColorToHSL(picked, &hsl);
  hsl.h += kHueTurn;
  if (hsl.h < 0) {
    hsl.h += 1;
  }
  return {SkColorSetA(picked, SK_AlphaOPAQUE),
          color_utils::HSLToSkColor(hsl, SK_AlphaOPAQUE)};
}

SkColor MiddleOf(const Tint& tint) {
  return color_utils::AlphaBlend(tint.start, tint.end, 0.5f);
}

SkColor TextColorOn(const Tint& tint) {
  const SkColor middle = MiddleOf(tint);
  return color_utils::GetContrastRatio(middle, kDarkTextOnTint) >=
                 color_utils::GetContrastRatio(middle, kLightTextOnTint)
             ? kDarkTextOnTint
             : kLightTextOnTint;
}

bool IsDark(const Tint& tint) {
  return TextColorOn(tint) == kLightTextOnTint;
}

SkColor FillOn(const Tint& tint) {
  return color_utils::AlphaBlend(SK_ColorWHITE, MiddleOf(tint),
                                 IsDark(tint) ? kFillOnDark : kFillOnLight);
}

SkColor ActiveTabOn(const Tint& tint) {
  return IsDark(tint) ? color_utils::AlphaBlend(SK_ColorWHITE, MiddleOf(tint),
                                                kActiveTabOnDark)
                      : SK_ColorWHITE;
}

}  // namespace uw
