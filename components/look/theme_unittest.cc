// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/components/look/theme.h"

#include "components/prefs/testing_pref_service.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/gfx/color_utils.h"
#include "uw/components/look/theme_prefs.h"

namespace uw {
namespace {

constexpr SkColor kSky = SkColorSetRGB(0xD8, 0xE6, 0xF9);
constexpr SkColor kButter = SkColorSetRGB(0xF7, 0xE0, 0x8A);
constexpr SkColor kWine = SkColorSetRGB(0x5A, 0x1F, 0x3A);

color_utils::HSL HslOf(SkColor color) {
  color_utils::HSL hsl;
  color_utils::SkColorToHSL(color, &hsl);
  return hsl;
}

class ThemePrefsTest : public testing::Test {
 protected:
  ThemePrefsTest() { RegisterThemeProfilePrefs(prefs_.registry()); }

  TestingPrefServiceSimple prefs_;
};

TEST(ThemeTest, LilacIsTheDefaultAndTheFirstSuggestion) {
  EXPECT_EQ(Theme().colors, std::vector<SkColor>{kDefaultLightTint});
  EXPECT_EQ(SuggestedThemes().front(), std::vector<SkColor>{kDefaultLightTint});
  EXPECT_EQ(SuggestedThemes().size(), 10u);
}

TEST(ThemeTest, IntensityScalesSaturation) {
  EXPECT_EQ(WithIntensity(kWine, kFullIntensity), kWine);
  EXPECT_NEAR(HslOf(WithIntensity(kWine, 50)).s, HslOf(kWine).s / 2, 0.02);
  EXPECT_NEAR(HslOf(WithIntensity(kWine, 0)).s, 0, 0.01);
}

TEST(ThemeTest, DarkModeKeepsTheHueAt14PercentLightness) {
  const color_utils::HSL dark = HslOf(DarkModeColorOf(kSky));
  EXPECT_NEAR(dark.l, 0.14, 0.01);
  EXPECT_NEAR(dark.h, HslOf(kSky).h, 0.01);
}

TEST(ThemeTest, EveryGradientColorKeepsTheTextReadable) {
  for (const std::vector<SkColor>& colors : SuggestedThemes()) {
    Theme theme;
    theme.colors = colors;
    for (bool dark : {false, true}) {
      const SkColor text = dark ? kLightTextOnTint : kDarkTextOnTint;
      for (SkColor color : MoreGradientColors(theme, dark, text)) {
        EXPECT_GE(color_utils::GetContrastRatio(color, text),
                  kReadableContrast);
      }
    }
  }
  Theme lilac_and_wine;
  lilac_and_wine.colors = {kDefaultLightTint, kWine};
  const std::vector<SkColor> more =
      MoreGradientColors(lilac_and_wine, false, kDarkTextOnTint);
  ASSERT_EQ(more.size(), 1u);
  EXPECT_GT(HslOf(more[0]).l, HslOf(kWine).l);
}

TEST(ThemeTest, TransparencyRunsFromSolidToAFifth) {
  EXPECT_EQ(Theme().transparency, 0);
  EXPECT_FLOAT_EQ(WindowCover(0), 1.0f);
  EXPECT_FLOAT_EQ(WindowCover(kFullTransparency / 2), 0.6f);
  EXPECT_FLOAT_EQ(WindowCover(kFullTransparency), 0.2f);
  EXPECT_FLOAT_EQ(WindowCover(-5), 1.0f);
  EXPECT_FLOAT_EQ(WindowCover(kFullTransparency + 5), 0.2f);
}

TEST(ThemeTest, TheReadableTransparencyIsWhereTheTickSits) {
  EXPECT_EQ(ReadableTransparency(kDarkTextOnTint), 50);
  EXPECT_EQ(ReadableTransparency(kLightTextOnTint), 31);
  EXPECT_GE(WindowCover(ReadableTransparency(kLightTextOnTint)), 0.75f);
}

TEST(ThemeTest, GlassRunsFromFrostedToClear) {
  EXPECT_EQ(Theme().glass, 0);
  EXPECT_FLOAT_EQ(GlassTint(0), 1.0f);
  EXPECT_FLOAT_EQ(GlassTint(kFullGlass), 0.15f);
  EXPECT_GT(GlassTint(1), GlassTint(kFullGlass / 2));
}

TEST(ThemeTest, TheToolbarCoversTheGlassEnoughForItsText) {
  EXPECT_NEAR(ReadableCover(SK_ColorWHITE, kDarkTextOnTint), 0.52f, 0.011f);
  EXPECT_NEAR(ReadableCover(SkColorSetRGB(0xC6, 0x28, 0x28), SK_ColorWHITE),
              0.86f, 0.011f);
  EXPECT_NEAR(ReadableCover(kBlackCard, SK_ColorWHITE), 0.56f, 0.011f);
  for (SkColor desktop : {SK_ColorWHITE, SK_ColorBLACK}) {
    const float cover = ReadableCover(kBlackCard, SK_ColorWHITE);
    EXPECT_GE(
        color_utils::GetContrastRatio(
            color_utils::AlphaBlend(kBlackCard, desktop, cover), SK_ColorWHITE),
        kReadableContrast);
  }
}

TEST_F(ThemePrefsTest, WithNoUserColorTheThemeIsTheDefault) {
  prefs_.SetInteger(kThemeGrainPref, 40);
  const Theme theme = ReadTheme(prefs_, std::nullopt);
  EXPECT_EQ(theme.colors, Theme().colors);
  EXPECT_EQ(theme.grain, 40);
}

TEST_F(ThemePrefsTest, ASavedThemeComesBackWhole) {
  Theme theme;
  theme.colors = {kButter, kSky, kWine};
  theme.intensity = 60;
  theme.grain = 25;
  theme.transparency = 70;
  theme.glass = 20;
  theme.colors_in_dark_mode = true;
  theme.page_mode = PageMode::kDark;
  WriteTheme(prefs_, theme);
  EXPECT_EQ(ReadTheme(prefs_, WithIntensity(kButter, 60)), theme);
}

TEST_F(ThemePrefsTest, AColorPickedElsewhereIsAOneColorTheme) {
  Theme theme;
  theme.colors = {kButter, kSky};
  theme.intensity = 60;
  theme.grain = 25;
  WriteTheme(prefs_, theme);
  const Theme read = ReadTheme(prefs_, kWine);
  EXPECT_EQ(read.colors, std::vector<SkColor>{kWine});
  EXPECT_EQ(read.intensity, kFullIntensity);
  EXPECT_EQ(read.grain, 25);
}

TEST_F(ThemePrefsTest, TheGradientRunsThroughEveryColorOfTheTheme) {
  Theme theme;
  theme.colors = {kButter, kSky};
  EXPECT_EQ(GradientColorsFor(theme, TintFrom(kButter).start),
            (std::vector<SkColor>{TintFrom(kButter).start, kSky}));
  EXPECT_TRUE(GradientColorsFor(theme, kWine).empty());
  EXPECT_TRUE(GradientColorsFor(theme, SK_ColorBLACK).empty());

  const SkColor dark_butter = TintFrom(DarkModeColorOf(kButter)).start;
  EXPECT_TRUE(GradientColorsFor(theme, dark_butter).empty());
  theme.colors_in_dark_mode = true;
  EXPECT_EQ(GradientColorsFor(theme, dark_butter).size(), 2u);
}

TEST_F(ThemePrefsTest, AWorkspaceKeepsEveryPartOfItsTheme) {
  Theme theme;
  theme.colors = {kButter, kSky};
  theme.intensity = 60;
  theme.grain = 25;
  theme.transparency = 40;
  theme.glass = 90;
  theme.colors_in_dark_mode = true;
  theme.mode = ThemeMode::kDark;
  theme.page_mode = PageMode::kLight;
  EXPECT_EQ(ThemeFromValue(ThemeToValue(theme)), theme);
  EXPECT_FALSE(ThemeFromValue(base::DictValue()));
}

TEST_F(ThemePrefsTest, ATintFromBeforeThemesIsTheTheme) {
  EXPECT_EQ(ReadTheme(prefs_, kWine).colors, std::vector<SkColor>{kWine});
}

}  // namespace
}  // namespace uw
