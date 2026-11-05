// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/components/look/theme.h"

#include "base/values.h"
#include "components/prefs/testing_pref_service.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/gfx/color_utils.h"
#include "uw/components/look/theme_prefs.h"

namespace uw {
namespace {

constexpr SkColor kButter = SkColorSetRGB(0xF7, 0xE0, 0x8A);
constexpr SkColor kMint = SkColorSetRGB(0xCF, 0xEF, 0xDD);
constexpr SkColor kWine = SkColorSetRGB(0x5A, 0x1F, 0x3A);

class ThemePrefsTest : public testing::Test {
 protected:
  ThemePrefsTest() { RegisterThemeProfilePrefs(prefs_.registry()); }

  TestingPrefServiceSimple prefs_;
};

Theme ThemeOf(const SuggestedTheme& suggestion) {
  Theme theme;
  theme.colors = suggestion.colors;
  theme.brightness = suggestion.brightness;
  return theme;
}

TEST(ThemeTest, LilacToSkyIsTheDefaultAndTheFirstSuggestion) {
  const std::vector<SkColor> lilac_to_sky = {kDefaultLightTint, kSky};
  EXPECT_EQ(Theme().colors, lilac_to_sky);
  EXPECT_EQ(Theme().brightness, kFullBrightness);
  EXPECT_EQ(SuggestedThemes().front(), SuggestedTheme{lilac_to_sky});
  EXPECT_EQ(SuggestedThemes().size(), 11u);
}

TEST(ThemeTest, BlackIsAnyColorAtNoBrightness) {
  const SuggestedTheme black = SuggestedThemes()[8];
  EXPECT_EQ(black.brightness, 0);
  EXPECT_EQ(ColorAsShown(ThemeOf(black), black.colors[0]), SK_ColorBLACK);
}

TEST(ThemeTest, IntensityScalesChroma) {
  EXPECT_EQ(WithIntensity(kWine, kFullIntensity), kWine);
  EXPECT_NEAR(OklchOf(WithIntensity(kWine, 50)).c, OklchOf(kWine).c / 2,
              0.005f);
  EXPECT_NEAR(OklchOf(WithIntensity(kWine, 50)).l, OklchOf(kWine).l, 0.01f);
  EXPECT_TRUE(IsNeutral(WithIntensity(kWine, 0)));
}

TEST(ThemeTest, BrightnessScalesLightnessAndKeepsTheHue) {
  EXPECT_EQ(WithBrightness(kButter, kFullBrightness), kButter);
  const Oklch half = OklchOf(WithBrightness(kButter, 50));
  EXPECT_NEAR(half.l, OklchOf(kButter).l / 2, 0.01f);
  EXPECT_NEAR(half.h, OklchOf(kButter).h, 3);
  EXPECT_EQ(WithBrightness(kButter, 0), SK_ColorBLACK);
}

TEST(ThemeTest, AThemeWithWhiteTextIsDark) {
  EXPECT_FALSE(IsDarkTheme(Theme()));
  Theme mint;
  mint.colors = {kMint};
  mint.brightness = 80;
  EXPECT_FALSE(IsDarkTheme(mint));
  mint.brightness = 60;
  EXPECT_TRUE(IsDarkTheme(mint));
  for (size_t dark : {6u, 7u, 8u}) {
    EXPECT_TRUE(IsDarkTheme(ThemeOf(SuggestedThemes()[dark]))) << dark;
  }
}

TEST(ThemeTest, OnlyColorsWithoutAHueAreNeutral) {
  for (SkColor neutral : {SK_ColorBLACK, SK_ColorWHITE,
                          SkColorSetRGB(0x80, 0x80, 0x80)}) {
    EXPECT_TRUE(IsNeutral(neutral));
  }
  for (SkColor colored : {kDefaultLightTint, kSky, kWine}) {
    EXPECT_FALSE(IsNeutral(colored));
  }
}

TEST(ThemeTest, EveryGradientColorKeepsTheTextReadable) {
  for (const SuggestedTheme& suggestion : SuggestedThemes()) {
    const Theme theme = ThemeOf(suggestion);
    const SkColor text =
        TextColorOn(TintFrom(ColorAsShown(theme, theme.colors[0])));
    for (SkColor color : MoreGradientColors(theme, text)) {
      EXPECT_GE(color_utils::GetContrastRatio(color, text), kReadableContrast);
    }
  }
  Theme lilac_and_wine;
  lilac_and_wine.colors = {kDefaultLightTint, kWine};
  const std::vector<SkColor> more =
      MoreGradientColors(lilac_and_wine, kDarkTextOnTint);
  ASSERT_EQ(more.size(), 1u);
  EXPECT_GT(OklchOf(more[0]).l, OklchOf(kWine).l);
}

TEST(ThemeTest, EveryPointOfTheFieldIsFoundAgainFromItsColor) {
  for (FieldPoint point : {FieldPoint{0.25f, 0.1f}, FieldPoint{0.5f, 0.3f},
                           FieldPoint{0.75f, kFieldPaleUntil}}) {
    const FieldPoint found = FieldPointOf(FieldColorAt(point));
    EXPECT_NEAR(found.x, point.x, 0.01f);
    EXPECT_NEAR(found.y, point.y, 0.02f);
  }
  EXPECT_EQ(FieldColorAt({0.3f, kFieldPaleUntil}),
            FieldColorAt({0.3f, 1.0f}));
  EXPECT_GT(OklchOf(FieldColorAt({0.3f, 0})).l, 0.95f);
}

TEST(ThemeTest, WineAndForestSitOnTheField) {
  for (size_t dark : {6u, 7u}) {
    const SkColor color = SuggestedThemes()[dark].colors[0];
    EXPECT_EQ(FieldColorAt(FieldPointOf(color)), color) << dark;
  }
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
  prefs_.SetInteger(kThemeBrightnessPref, 30);
  const Theme theme = ReadTheme(prefs_, std::nullopt);
  EXPECT_EQ(theme.colors, Theme().colors);
  EXPECT_EQ(theme.brightness, kFullBrightness);
  EXPECT_EQ(theme.grain, 40);
}

TEST_F(ThemePrefsTest, ASavedThemeComesBackWhole) {
  Theme theme;
  theme.colors = {kButter, kSky, kWine};
  theme.intensity = 60;
  theme.brightness = 70;
  theme.grain = 25;
  theme.transparency = 70;
  theme.glass = 20;
  WriteTheme(prefs_, theme);
  EXPECT_EQ(ReadTheme(prefs_, ColorAsShown(theme, kButter)), theme);
}

TEST_F(ThemePrefsTest, AColorPickedElsewhereIsAOneColorTheme) {
  Theme theme;
  theme.colors = {kButter, kSky};
  theme.intensity = 60;
  theme.brightness = 70;
  theme.grain = 25;
  WriteTheme(prefs_, theme);
  const Theme read = ReadTheme(prefs_, kWine);
  EXPECT_EQ(read.colors, std::vector<SkColor>{kWine});
  EXPECT_EQ(read.intensity, kFullIntensity);
  EXPECT_EQ(read.brightness, kFullBrightness);
  EXPECT_EQ(read.grain, 25);
}

TEST_F(ThemePrefsTest, TheGradientRunsThroughEveryColorOfTheTheme) {
  Theme theme;
  theme.colors = {kButter, kSky};
  EXPECT_EQ(GradientColorsFor(theme, TintFrom(kButter).start),
            (std::vector<SkColor>{TintFrom(kButter).start, kSky}));
  EXPECT_TRUE(GradientColorsFor(theme, kWine).empty());

  theme.brightness = 0;
  EXPECT_EQ(GradientColorsFor(theme, SK_ColorBLACK),
            (std::vector<SkColor>{SK_ColorBLACK, SK_ColorBLACK}));
}

TEST_F(ThemePrefsTest, AWorkspaceKeepsEveryPartOfItsTheme) {
  Theme theme;
  theme.colors = {kButter, kSky};
  theme.intensity = 60;
  theme.brightness = 40;
  theme.grain = 25;
  theme.transparency = 40;
  theme.glass = 90;
  EXPECT_EQ(ThemeFromValue(ThemeToValue(theme)), theme);
  EXPECT_FALSE(ThemeFromValue(base::DictValue()));
}

TEST_F(ThemePrefsTest, AWorkspaceThemeFromBeforeKeepsItsColorsAtFullBrightness) {
  base::ListValue colors;
  colors.Append(static_cast<int>(kWine));
  const std::optional<Theme> theme =
      ThemeFromValue(base::DictValue()
                         .Set("colors", std::move(colors))
                         .Set("mode", "dark")
                         .Set("colors_in_dark_mode", true));
  ASSERT_TRUE(theme);
  EXPECT_EQ(theme->colors, std::vector<SkColor>{kWine});
  EXPECT_EQ(theme->brightness, kFullBrightness);
}

TEST_F(ThemePrefsTest, ThePagesRowFromBeforeIsReadOnce) {
  EXPECT_EQ(TakeOldPagesRowDark(prefs_), std::nullopt);
  prefs_.SetString(kThemePageModePref, "dark");
  EXPECT_EQ(TakeOldPagesRowDark(prefs_), true);
  EXPECT_EQ(TakeOldPagesRowDark(prefs_), std::nullopt);
  prefs_.SetString(kThemePageModePref, "light");
  EXPECT_EQ(TakeOldPagesRowDark(prefs_), false);
  prefs_.SetString(kThemePageModePref, "window");
  EXPECT_EQ(TakeOldPagesRowDark(prefs_), std::nullopt);
}

TEST_F(ThemePrefsTest, ATintFromBeforeThemesIsTheTheme) {
  EXPECT_EQ(ReadTheme(prefs_, kWine).colors, std::vector<SkColor>{kWine});
}

}  // namespace
}  // namespace uw
