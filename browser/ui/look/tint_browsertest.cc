// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/components/look/tint.h"

#include <algorithm>
#include <cstdlib>

#include "base/numerics/safe_conversions.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/headless/test/headless_mode_browsertest.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/profiles/profile_test_util.h"
#include "chrome/browser/themes/theme_service.h"
#include "chrome/browser/themes/theme_service_factory.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/color/chrome_color_id.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/multi_contents_view.h"
#include "chrome/browser/ui/views/frame/vertical_tab_strip_region_view.h"
#include "chrome/browser/ui/views/toolbar/toolbar_view.h"
#include "chrome/common/pref_names.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/prefs/pref_service.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/base/mojom/themes.mojom.h"
#include "ui/color/color_id.h"
#include "ui/color/color_provider.h"
#include "ui/gfx/geometry/point.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/views/widget/widget.h"
#include "url/gurl.h"
#include "uw/browser/tab_organization/window_screenshot.h"
#include "uw/browser/ui/look/page_card.h"
#include "uw/components/look/theme.h"
#include "uw/components/look/theme_prefs.h"

namespace uw {
namespace {

constexpr SkColor kButter = SkColorSetRGB(0xF7, 0xE0, 0x8A);
constexpr SkColor kWine = SkColorSetRGB(0x5A, 0x1F, 0x3A);
constexpr SkColor kSky = SkColorSetRGB(0xD8, 0xE6, 0xF9);

int Channel(SkColor color, int shift) {
  return (color >> shift) & 0xFF;
}

// The largest difference between two colors in any one channel.
int Gap(SkColor first, SkColor second) {
  int gap = 0;
  for (int shift : {0, 8, 16}) {
    gap =
        std::max(gap, std::abs(Channel(first, shift) - Channel(second, shift)));
  }
  return gap;
}

class TintBrowserTest : public headless::HeadlessModeBrowserTest {
 protected:
  ThemeService* theme() {
    return ThemeServiceFactory::GetForProfile(browser()->GetProfile());
  }

  SkColor Color(ui::ColorId id) { return Color(browser(), id); }

  SkColor Color(Browser* window, ui::ColorId id) {
    return BrowserView::GetBrowserViewForBrowser(window)
        ->GetColorProvider()
        ->GetColor(id);
  }

  // Saves `theme` as the picker does: in the profile's prefs, with its first
  // color at its intensity as Chromium's user color.
  void SetTheme(Profile* profile, const Theme& theme) {
    WriteTheme(*profile->GetPrefs(), theme);
    ThemeServiceFactory::GetForProfile(profile)
        ->SetUserColorAndBrowserColorVariant(
            WithIntensity(theme.colors[0], theme.intensity),
            ui::mojom::BrowserColorVariant::kTonalSpot);
  }

  void Pick(SkColor color) {
    theme()->SetUserColorAndBrowserColorVariant(
        color, ui::mojom::BrowserColorVariant::kTonalSpot);
  }
};

IN_PROC_BROWSER_TEST_F(TintBrowserTest, DefaultTintFollowsLightAndDarkMode) {
  theme()->SetBrowserColorScheme(ThemeService::BrowserColorScheme::kLight);
  EXPECT_EQ(Color(ui::kColorFrameActive), kDefaultLightTint);
  EXPECT_EQ(Color(kColorTabForegroundInactiveFrameActive), kDarkTextOnTint);
  EXPECT_EQ(Color(kColorTabBackgroundActiveFrameActive), SK_ColorWHITE);

  theme()->SetBrowserColorScheme(ThemeService::BrowserColorScheme::kDark);
  EXPECT_EQ(Color(ui::kColorFrameActive), SK_ColorBLACK);
  EXPECT_EQ(Color(kColorTabForegroundInactiveFrameActive), kLightTextOnTint);
  EXPECT_EQ(Color(kColorToolbar), kBlackCard);
  EXPECT_EQ(Color(kColorLocationBarBackground), kBlackCardField);
  EXPECT_EQ(Color(kColorOmniboxResultsBackground), kBlackCardField);
  EXPECT_EQ(Color(kColorToolbarContentAreaSeparator), kBlackCardLine);
  EXPECT_EQ(Color(kColorPageCardEdge), kBlackCardEdge);

  theme()->SetBrowserColorScheme(ThemeService::BrowserColorScheme::kLight);
  EXPECT_EQ(Color(kColorPageCardEdge), SK_ColorTRANSPARENT);
}

// Dark mode is black even with a color picked; light mode keeps the color.
IN_PROC_BROWSER_TEST_F(TintBrowserTest, DarkModeIsBlackWhateverColorIsPicked) {
  theme()->SetBrowserColorScheme(ThemeService::BrowserColorScheme::kDark);
  const SkColor accent = Color(ui::kColorSysPrimary);
  Pick(kWine);
  EXPECT_EQ(Color(ui::kColorFrameActive), SK_ColorBLACK);
  EXPECT_EQ(Color(kColorToolbar), kBlackCard);
  EXPECT_EQ(Color(ui::kColorSysPrimary), accent);
  SaveWindowScreenshot(browser(), "dark-black");

  theme()->SetBrowserColorScheme(ThemeService::BrowserColorScheme::kLight);
  EXPECT_EQ(Color(ui::kColorFrameActive), kWine);
}

IN_PROC_BROWSER_TEST_F(TintBrowserTest, APickedColorBecomesTheTint) {
  theme()->SetBrowserColorScheme(ThemeService::BrowserColorScheme::kLight);
  Pick(kWine);
  EXPECT_EQ(Color(ui::kColorFrameActive), kWine);
  EXPECT_EQ(Color(ui::kColorFrameInactive), kWine);
  EXPECT_EQ(Color(kColorTabForegroundInactiveFrameActive), kLightTextOnTint);
  EXPECT_EQ(Color(kColorTabForegroundActiveFrameActive), kLightTextOnTint);
  SaveWindowScreenshot(browser(), "tint-wine");

  Pick(kButter);
  EXPECT_EQ(Color(ui::kColorFrameActive), kButter);
  EXPECT_EQ(Color(kColorTabForegroundInactiveFrameActive), kDarkTextOnTint);
  EXPECT_EQ(Color(kColorTabBackgroundActiveFrameActive), SK_ColorWHITE);
  SaveWindowScreenshot(browser(), "tint-butter");
}

// The sidebar and the window frame behind the page card each paint their
// part of the tint. A seam between them shows as a band of another color
// above and below the card.
IN_PROC_BROWSER_TEST_F(TintBrowserTest, SidebarAndFrameDrawOneGradient) {
  theme()->SetBrowserColorScheme(ThemeService::BrowserColorScheme::kLight);
  const SkBitmap window = CaptureWindow(browser());
  BrowserView* view = BrowserView::GetBrowserViewForBrowser(browser());
  const gfx::Rect bounds = view->GetWidget()->GetWindowBoundsInScreen();
  const float scale = static_cast<float>(window.width()) / bounds.width();
  const int seam =
      base::ClampRound((view->vertical_tab_strip_region_view_for_testing()
                            ->GetBoundsInScreen()
                            .right() -
                        bounds.x()) *
                       scale);
  const int reach = base::ClampRound(24 * scale);
  const int inset = base::ClampRound(3 * scale);
  for (int y : {inset, window.height() - inset}) {
    EXPECT_LE(
        Gap(window.getColor(seam - reach, y), window.getColor(seam + reach, y)),
        3)
        << "at y=" << y;
  }

  EXPECT_NE(window.getColor(seam + reach, inset),
            window.getColor(seam + reach, window.height() - inset));
}

// A square corner shows the toolbar or the page where the tint should be.
IN_PROC_BROWSER_TEST_F(TintBrowserTest, ThePageCardHasFourRoundCorners) {
  theme()->SetBrowserColorScheme(ThemeService::BrowserColorScheme::kDark);
  const SkBitmap window = CaptureWindow(browser());
  BrowserView* view = BrowserView::GetBrowserViewForBrowser(browser());
  const gfx::Rect bounds = view->GetWidget()->GetWindowBoundsInScreen();
  const float scale = static_cast<float>(window.width()) / bounds.width();
  gfx::Rect card = view->toolbar()->GetBoundsInScreen();
  card.Union(view->multi_contents_view()->GetBoundsInScreen());
  card.Offset(-bounds.OffsetFromOrigin());
  const auto color_at = [&](gfx::Point corner, int x, int y) {
    return window.getColor(base::ClampRound((corner.x() + x) * scale),
                           base::ClampRound((corner.y() + y) * scale));
  };

  struct Corner {
    const char* name;
    gfx::Point point;
    int into_card_x;
    int into_card_y;
  };
  const Corner corners[] = {
      {"top left", card.origin(), 1, 1},
      {"top right", card.top_right(), -1, 1},
      {"bottom left", card.bottom_left(), 1, -1},
      {"bottom right", card.bottom_right(), -1, -1},
  };
  for (const Corner& corner : corners) {
    const SkColor inside =
        color_at(corner.point, corner.into_card_x, corner.into_card_y);
    const SkColor outside = color_at(corner.point, -4 * corner.into_card_x,
                                     -4 * corner.into_card_y);
    EXPECT_LE(Gap(inside, outside), 3) << corner.name;
  }
  const gfx::Point center = card.CenterPoint();
  EXPECT_GT(Gap(color_at(center, 0, 0), color_at(card.origin(), -4, -4)), 3);
}

// On black, the card shows by its gray edge just outside it.
IN_PROC_BROWSER_TEST_F(TintBrowserTest, TheBlackCardHasAGrayEdge) {
  theme()->SetBrowserColorScheme(ThemeService::BrowserColorScheme::kDark);
  const SkBitmap window = CaptureWindow(browser());
  BrowserView* view = BrowserView::GetBrowserViewForBrowser(browser());
  const gfx::Rect bounds = view->GetWidget()->GetWindowBoundsInScreen();
  const float scale = static_cast<float>(window.width()) / bounds.width();
  gfx::Rect card = view->toolbar()->GetBoundsInScreen();
  card.Union(view->multi_contents_view()->GetBoundsInScreen());
  card.Offset(-bounds.OffsetFromOrigin());
  const auto color_at = [&](float x, float y) {
    return window.getColor(base::ClampFloor(x * scale),
                           base::ClampFloor(y * scale));
  };
  const float middle_x = card.CenterPoint().x();
  const float middle_y = card.CenterPoint().y();
  EXPECT_LE(Gap(color_at(middle_x, card.y() - 0.5f), kBlackCardEdge), 3);
  EXPECT_LE(Gap(color_at(middle_x, card.bottom() + 0.5f), kBlackCardEdge), 3);
  EXPECT_LE(Gap(color_at(card.right() + 0.5f, middle_y), kBlackCardEdge), 3);
  EXPECT_LE(Gap(color_at(card.x() - 0.5f, middle_y), kBlackCardEdge), 3);
  EXPECT_LE(Gap(color_at(middle_x, card.y() - 4), SK_ColorBLACK), 3);
}

// uw's own pages use the same blacks in dark mode.
IN_PROC_BROWSER_TEST_F(TintBrowserTest, TheChatPageIsBlackInDarkMode) {
  theme()->SetBrowserColorScheme(ThemeService::BrowserColorScheme::kDark);
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), GURL("chrome://chat/")));
  EXPECT_EQ(
      content::EvalJs(browser()->tab_strip_model()->GetActiveWebContents(),
                      "getComputedStyle(document.documentElement)"
                      ".getPropertyValue('--background').trim()"),
      "#0a0a0a");
  SaveWindowScreenshot(browser(), "chat-dark");
}

// Two colors run from the first, at the top leading corner, to the second, at
// the bottom trailing corner.
IN_PROC_BROWSER_TEST_F(TintBrowserTest, ATwoColorThemeRunsThroughBothColors) {
  theme()->SetBrowserColorScheme(ThemeService::BrowserColorScheme::kLight);
  Theme butter_to_sky;
  butter_to_sky.colors = {kButter, kSky};
  SetTheme(browser()->GetProfile(), butter_to_sky);
  const SkBitmap window = CaptureWindow(browser());
  const int width = window.width();
  const int height = window.height();
  EXPECT_LE(Gap(window.getColor(width / 40, 3), kButter), 6);
  EXPECT_LE(Gap(window.getColor(width - width / 40, height - 3), kSky), 6);
  SaveWindowScreenshot(browser(), "theme-two-colors");
}

IN_PROC_BROWSER_TEST_F(TintBrowserTest, AColorPickedElsewhereIsAOneColorTheme) {
  theme()->SetBrowserColorScheme(ThemeService::BrowserColorScheme::kLight);
  Theme butter_to_sky;
  butter_to_sky.colors = {kButter, kSky};
  SetTheme(browser()->GetProfile(), butter_to_sky);
  Pick(kWine);
  EXPECT_EQ(ReadTheme(*browser()->GetProfile()->GetPrefs(), kWine).colors,
            std::vector<SkColor>{kWine});
  const SkBitmap window = CaptureWindow(browser());
  EXPECT_LE(Gap(window.getColor(window.width() - window.width() / 40,
                                window.height() - 3),
                TintFrom(kWine).end),
            6);
}

IN_PROC_BROWSER_TEST_F(TintBrowserTest, ThemeColorsInDarkModeDarkenTheTheme) {
  theme()->SetBrowserColorScheme(ThemeService::BrowserColorScheme::kDark);
  Theme wine;
  wine.colors = {kWine};
  wine.colors_in_dark_mode = true;
  SetTheme(browser()->GetProfile(), wine);
  EXPECT_EQ(Color(ui::kColorFrameActive),
            TintFrom(DarkModeColorOf(kWine)).start);
  EXPECT_EQ(Color(kColorToolbar), kBlackCard);
  SaveWindowScreenshot(browser(), "dark-theme-colors");

  wine.colors_in_dark_mode = false;
  SetTheme(browser()->GetProfile(), wine);
  EXPECT_EQ(Color(ui::kColorFrameActive), SK_ColorBLACK);
}

IN_PROC_BROWSER_TEST_F(TintBrowserTest, TwoProfilesKeepTwoThemes) {
  ProfileManager* manager = g_browser_process->profile_manager();
  Profile& second = profiles::testing::CreateProfileSync(
      manager, manager->GenerateNextProfileDirectoryPath());
  Browser* other = CreateBrowser(&second);
  for (Profile* profile : {browser()->GetProfile(), &second}) {
    ThemeServiceFactory::GetForProfile(profile)->SetBrowserColorScheme(
        ThemeService::BrowserColorScheme::kLight);
  }
  Theme butter;
  butter.colors = {kButter};
  SetTheme(browser()->GetProfile(), butter);
  Theme wine;
  wine.colors = {kWine};
  SetTheme(&second, wine);
  EXPECT_EQ(Color(ui::kColorFrameActive), kButter);
  EXPECT_EQ(Color(other, ui::kColorFrameActive), kWine);
}

// Grain shows on the window around the card and never on the card.
IN_PROC_BROWSER_TEST_F(TintBrowserTest, GrainCoversTheWindowAndNotTheCard) {
  theme()->SetBrowserColorScheme(ThemeService::BrowserColorScheme::kLight);
  const auto roughness = [](const SkBitmap& window, int x, int y) {
    int most = 0;
    for (int step = 0; step < 40; ++step) {
      most = std::max(most, Gap(window.getColor(x + step, y),
                                window.getColor(x + step + 1, y)));
    }
    return most;
  };
  const SkBitmap smooth = CaptureWindow(browser());
  const int middle = smooth.height() / 2;
  EXPECT_LE(roughness(smooth, 20, middle), 2);

  Theme grainy;
  grainy.grain = kFullGrain;
  SetTheme(browser()->GetProfile(), grainy);
  const SkBitmap rough = CaptureWindow(browser());
  EXPECT_GE(roughness(rough, 20, middle), 8);
  EXPECT_LE(roughness(rough, rough.width() * 3 / 5, middle), 1);
  SaveWindowScreenshot(browser(), "theme-grain");
}

IN_PROC_BROWSER_TEST_F(TintBrowserTest, GlassFrameIsOffUnlessTurnedOn) {
  EXPECT_FALSE(
      g_browser_process->local_state()->GetBoolean(prefs::kGlassFrameEnabled));
}

}  // namespace
}  // namespace uw
