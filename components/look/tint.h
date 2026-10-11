// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#ifndef UW_COMPONENTS_LOOK_TINT_H_
#define UW_COMPONENTS_LOOK_TINT_H_

#include "third_party/skia/include/core/SkColor.h"

namespace uw {

// The tints used until the user picks a color.
inline constexpr SkColor kDefaultLightTint = SkColorSetRGB(0xE7, 0xE1, 0xF6);
inline constexpr SkColor kDefaultDarkTint = SkColorSetRGB(0x2B, 0x25, 0x42);

// Text on a tint is one of these two.
inline constexpr SkColor kDarkTextOnTint = SkColorSetRGB(0x1E, 0x1B, 0x2E);
inline constexpr SkColor kLightTextOnTint = SK_ColorWHITE;

// The window tint is a gradient from one color to its neighbor on the color
// wheel.
struct Tint {
  SkColor start;
  SkColor end;
};

// The tint for one picked color: the color itself, then the same color with
// its hue turned by 40 degrees at the same saturation and lightness.
Tint TintFrom(SkColor picked);

// The color halfway along the tint.
SkColor MiddleOf(const Tint& tint);

// Near-black or white, whichever has more contrast with the middle of the
// tint.
SkColor TextColorOn(const Tint& tint);

// True when the tint takes white text.
bool IsDark(const Tint& tint);

// What tiles, pills, and hovered rows are filled with on the tint.
SkColor FillOn(const Tint& tint);

// What the active tab is filled with on the tint.
SkColor ActiveTabOn(const Tint& tint);

}  // namespace uw

#endif  // UW_COMPONENTS_LOOK_TINT_H_
