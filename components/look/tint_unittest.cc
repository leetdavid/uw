// Copyright 2026 The uw Authors. All rights reserved.

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
constexpr SkColor kTomato = SkColorSetRGB(0xD9, 0x53, 0x4F);
constexpr SkColor kTeal = SkColorSetRGB(0x1F, 0x8A, 0x8A);

// How far `from` turns to `to`, from 0 to 360 degrees.
float Turn(SkColor from, SkColor to) {
  return std::fmod(OklchOf(to).h - OklchOf(from).h + 360.0f, 360.0f);
}

float Lightness(SkColor color) {
  return OklchOf(color).l;
}

TEST(TintTest, OklchComesBackToTheSameColor) {
  for (SkColor color : {kButter, kWine, kRoyal, kIndigo, SK_ColorWHITE,
                        SK_ColorBLACK, kDefaultLightTint}) {
    EXPECT_EQ(ColorOf(OklchOf(color)), color);
  }
  EXPECT_NEAR(OklchOf(SK_ColorWHITE).l, 1.0f, 0.001f);
  EXPECT_NEAR(OklchOf(SK_ColorBLACK).l, 0.0f, 0.001f);
  EXPECT_NEAR(OklchOf(SK_ColorWHITE).c, 0.0f, 0.001f);
}

TEST(TintTest, AColorTooColorfulForTheScreenLosesChromaNotHue) {
  const SkColor fitted = ColorOf({0.6f, 0.4f, 260.0f});
  EXPECT_NEAR(OklchOf(fitted).h, 260.0f, 3.0f);
  EXPECT_LT(OklchOf(fitted).c, 0.4f);
}

TEST(TintTest, StartsAtThePickedColor) {
  EXPECT_EQ(TintFrom(kButter).start, kButter);
  EXPECT_EQ(TintFrom(SkColorSetA(kButter, 0x40)).start, kButter);
}

TEST(TintTest, EndsFortyDegreesAroundTheWheelAtTheSameLightness) {
  const Tint butter = TintFrom(kButter);
  EXPECT_NEAR(Turn(butter.start, butter.end), 320, 2);
  EXPECT_NEAR(Lightness(butter.end), Lightness(butter.start), 0.01);

  const Tint dark = TintFrom(kIndigo);
  EXPECT_NEAR(Turn(dark.start, dark.end), 320, 2);
}

TEST(TintTest, HueTurnWrapsPastZero) {
  const SkColor rose = SkColorSetRGB(0xE8, 0x5A, 0x7A);
  ASSERT_LT(OklchOf(rose).h, 40);
  const Tint tint = TintFrom(rose);
  EXPECT_GT(OklchOf(tint.end).h, 320);
  EXPECT_NEAR(Turn(tint.start, tint.end), 320, 2);
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
  constexpr SkColor kGray = SkColorSetRGB(0x80, 0x80, 0x80);
  for (SkColor picked : {kTomato, kTeal, kGray}) {
    const Tint tint = TintFrom(picked);
    EXPECT_NE(tint.start, picked);
    for (SkColor background : {tint.start, tint.end}) {
      EXPECT_GE(color_utils::GetContrastRatio(background, TextColorOn(tint)),
                kReadableContrast);
    }
    EXPECT_LT(std::abs(Lightness(tint.start) - Lightness(picked)), 0.2f);
  }
}

// Royal blue reads as it is, so it keeps its exact color.
TEST(TintTest, AReadablePickIsNotNudged) {
  EXPECT_EQ(TintFrom(kRoyal).start, kRoyal);
}

TEST(TintTest, ANudgedPickKeepsItsHue) {
  const Tint tint = TintFrom(kTeal);
  ASSERT_NE(tint.start, kTeal);
  EXPECT_NEAR(OklchOf(tint.start).h, OklchOf(kTeal).h, 3);
  EXPECT_NEAR(Turn(tint.start, tint.end), 320, 3);
}

TEST(TintTest, ActiveTabIsWhiteOnLightAndTranslucentOnDark) {
  EXPECT_EQ(ActiveTabOn(TintFrom(kDefaultLightTint)), SK_ColorWHITE);
  const Tint dark = TintFrom(kIndigo);
  EXPECT_GT(color_utils::GetRelativeLuminance(ActiveTabOn(dark)),
            color_utils::GetRelativeLuminance(MiddleOf(dark)));
}

}  // namespace
}  // namespace uw
