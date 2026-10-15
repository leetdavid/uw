// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#ifndef UW_COMPONENTS_LOOK_TINT_H_
#define UW_COMPONENTS_LOOK_TINT_H_

#include "third_party/skia/include/core/SkColor.h"

namespace uw {

// The tint used in light mode until the user picks a color.
inline constexpr SkColor kDefaultLightTint = SkColorSetRGB(0xE7, 0xE1, 0xF6);

// Dark mode is black, after Vercel's neutral blacks: the window is black and
// the page card and its toolbar near-black, with a gray edge.
inline constexpr SkColor kBlackCard = SkColorSetRGB(0x0A, 0x0A, 0x0A);
inline constexpr SkColor kBlackCardEdge = SkColorSetRGB(0x26, 0x26, 0x26);
inline constexpr SkColor kBlackCardLine = SkColorSetRGB(0x1F, 0x1F, 0x1F);
inline constexpr SkColor kBlackCardField = SkColorSetRGB(0x17, 0x17, 0x17);

// Text on a tint is one of these two.
inline constexpr SkColor kDarkTextOnTint = SkColorSetRGB(0x1E, 0x1B, 0x2E);
inline constexpr SkColor kLightTextOnTint = SK_ColorWHITE;

// The window tint is a gradient from one color to its neighbor on the color
// wheel.
struct Tint {
  SkColor start;
  SkColor end;
};

// Text on the tint reaches at least this contrast ratio everywhere on it.
inline constexpr float kReadableContrast = 4.5f;

// The tint for one picked color: the color itself, then the same color with
// its hue turned by 40 degrees at the same saturation and lightness. A color
// too close to mid-tone for readable text is first made lighter or darker,
// whichever takes the smaller change.
Tint TintFrom(SkColor picked);

// The color halfway along the tint.
SkColor MiddleOf(const Tint& tint);

// Near-black or white, whichever keeps more contrast across the whole tint.
SkColor TextColorOn(const Tint& tint);

// True when the tint takes white text.
bool IsDark(const Tint& tint);

// What tiles, pills, and hovered rows are filled with on the tint.
SkColor FillOn(const Tint& tint);

// What the active tab is filled with on the tint.
SkColor ActiveTabOn(const Tint& tint);

}  // namespace uw

#endif  // UW_COMPONENTS_LOOK_TINT_H_
