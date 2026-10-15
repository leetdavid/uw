// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/components/look/tint.h"

#include <cmath>

#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/gfx/color_utils.h"

namespace uw {
namespace {

constexpr SkColor kButter = SkColorSetRGB(0xF7, 0xE0, 0x8A);
constexpr SkColor kWine = SkColorSetRGB(0x5A, 0x1F, 0x3A);
constexpr SkColor kRoyal = SkColorSetRGB(0x27, 0x47, 0xC7);
constexpr SkColor kIndigo = SkColorSetRGB(0x2B, 0x25, 0x42);

double Hue(SkColor color) {
  color_utils::HSL hsl;
  color_utils::SkColorToHSL(color, &hsl);
  return hsl.h * 360;
}

double Lightness(SkColor color) {
  color_utils::HSL hsl;
  color_utils::SkColorToHSL(color, &hsl);
  return hsl.l;
}

TEST(TintTest, StartsAtThePickedColor) {
  EXPECT_EQ(TintFrom(kButter).start, kButter);
  EXPECT_EQ(TintFrom(SkColorSetA(kButter, 0x40)).start, kButter);
}

TEST(TintTest, EndsFortyDegreesAroundTheWheelAtTheSameLightness) {
  const Tint butter = TintFrom(kButter);
  EXPECT_EQ(butter.end, SkColorSetRGB(0xF7, 0x97, 0x8A));
  EXPECT_NEAR(Lightness(butter.end), Lightness(butter.start), 0.01);

  const Tint dark = TintFrom(kIndigo);
  EXPECT_NEAR(Hue(dark.start) - Hue(dark.end), 40, 2);
}

TEST(TintTest, HueTurnWrapsPastRed) {
  const SkColor orange = SkColorSetRGB(0xE8, 0x71, 0x0A);
  const Tint tint = TintFrom(orange);
  EXPECT_NEAR(Hue(tint.end), Hue(orange) + 320, 2);
}

TEST(TintTest, LightTintsTakeDarkText) {
  for (SkColor picked : {kDefaultLightTint, kButter, SK_ColorWHITE}) {
    const Tint tint = TintFrom(picked);
    EXPECT_EQ(TextColorOn(tint), kDarkTextOnTint);
    EXPECT_FALSE(IsDark(tint));
  }
}

TEST(TintTest, DarkTintsTakeWhiteText) {
  for (SkColor picked : {kIndigo, kWine, SK_ColorBLACK}) {
    const Tint tint = TintFrom(picked);
    EXPECT_EQ(TextColorOn(tint), kLightTextOnTint);
    EXPECT_TRUE(IsDark(tint));
  }
}

TEST(TintTest, DefaultTintsKeepTextReadable) {
  for (SkColor picked : {kDefaultLightTint, kIndigo}) {
    const Tint tint = TintFrom(picked);
    for (SkColor background : {tint.start, tint.end}) {
      EXPECT_GE(color_utils::GetContrastRatio(background, TextColorOn(tint)),
                4.5f);
    }
  }
}

TEST(TintTest, MidTonePicksAreNudgedUntilTextIsReadable) {
  constexpr SkColor kTomato = SkColorSetRGB(0xD9, 0x53, 0x4F);
  constexpr SkColor kTeal = SkColorSetRGB(0x1F, 0x8A, 0x8A);
  constexpr SkColor kGray = SkColorSetRGB(0x80, 0x80, 0x80);
  for (SkColor picked : {kRoyal, kTomato, kTeal, kGray}) {
    const Tint tint = TintFrom(picked);
    EXPECT_NE(tint.start, picked);
    for (SkColor background : {tint.start, tint.end}) {
      EXPECT_GE(color_utils::GetContrastRatio(background, TextColorOn(tint)),
                kReadableContrast);
    }
    EXPECT_LT(std::abs(Lightness(tint.start) - Lightness(picked)), 0.2);
  }
}

TEST(TintTest, ANudgedPickKeepsItsHue) {
  const Tint tint = TintFrom(kRoyal);
  EXPECT_NEAR(Hue(tint.start), Hue(kRoyal), 2);
  EXPECT_NEAR(Hue(tint.start) - Hue(tint.end), 40, 2);
}

TEST(TintTest, ActiveTabIsWhiteOnLightAndTranslucentOnDark) {
  EXPECT_EQ(ActiveTabOn(TintFrom(kDefaultLightTint)), SK_ColorWHITE);
  const Tint dark = TintFrom(kIndigo);
  EXPECT_GT(color_utils::GetRelativeLuminance(ActiveTabOn(dark)),
            color_utils::GetRelativeLuminance(FillOn(dark)));
  EXPECT_GT(color_utils::GetRelativeLuminance(FillOn(dark)),
            color_utils::GetRelativeLuminance(MiddleOf(dark)));
}

}  // namespace
}  // namespace uw
