// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/components/look/tint.h"

#include <algorithm>
#include <cstdlib>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/environment.h"
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
#include "chrome/browser/ui/tabs/tab_style.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/multi_contents_view.h"
#include "chrome/browser/ui/views/frame/vertical_tab_strip_region_view.h"
#include "chrome/browser/ui/views/toolbar/toolbar_view.h"
#include "chrome/common/pref_names.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/base/mojom/themes.mojom.h"
#include "ui/base/page_transition_types.h"
#include "ui/color/color_id.h"
#include "ui/color/color_provider.h"
#include "ui/color/color_provider_key.h"
#include "ui/color/color_provider_manager.h"
#include "ui/gfx/geometry/point.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/views/view.h"
#include "ui/views/widget/widget.h"
#include "url/gurl.h"
#include "uw/browser/tab_organization/window_screenshot.h"
#include "uw/browser/ui/look/page_card.h"
#include "uw/browser/ui/look/sidebar_tab_fill.h"
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

// The largest step between neighboring pixels along 40 pixels of a row.
int Roughness(const SkBitmap& window, int x, int y) {
  int most = 0;
  for (int step = 0; step < 40; ++step) {
    most = std::max(most, Gap(window.getColor(x + step, y),
                              window.getColor(x + step + 1, y)));
  }
  return most;
}

class TintBrowserTest : public headless::HeadlessModeBrowserTest {
 protected:
  ThemeService* theme() {
    return ThemeServiceFactory::GetForProfile(browser()->GetProfile());
  }

  SkColor Color(ui::ColorId id) { return Color(browser(), id); }

  // The window's own colors, without the page color on its toolbar.
  SkColor Color(Browser* window, ui::ColorId id) {
    ui::ColorProviderKey key = BrowserView::GetBrowserViewForBrowser(window)
                                   ->GetWidget()
                                   ->GetColorProviderKeyForTesting();
    key.app_controller = nullptr;
    return ui::ColorProviderManager::Get().GetColorProviderFor(key)->GetColor(
        id);
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

// uw's other pages are on the same blacks in dark mode: the providers page
// and the terminal on the page black, and the command bar's card one step up.
IN_PROC_BROWSER_TEST_F(TintBrowserTest, UwPagesAreBlackInDarkMode) {
  // The terminal's shell reads none of the user's startup files.
  base::Environment::Create()->SetVar("ZDOTDIR", "/var/empty");
  theme()->SetBrowserColorScheme(ThemeService::BrowserColorScheme::kDark);
  const auto open = [&](const std::string& url) {
    EXPECT_TRUE(ui_test_utils::NavigateToURL(browser(), GURL(url)));
    return browser()->tab_strip_model()->GetActiveWebContents();
  };
  const auto style = [](content::WebContents* page,
                        const std::string& property) {
    return content::EvalJs(page,
                           "getComputedStyle(document.documentElement)"
                           ".getPropertyValue('" +
                               property + "').trim()")
        .ExtractString();
  };

  EXPECT_EQ(style(open("chrome://ai/"), "--background"), "#0a0a0a");
  SaveWindowScreenshot(browser(), "providers-dark");

  EXPECT_EQ(content::EvalJs(open("chrome://terminal/"),
                            "getComputedStyle(document.body).backgroundColor"),
            "rgb(10, 10, 10)");
  SaveWindowScreenshot(browser(), "terminal-dark");

  EXPECT_EQ(style(open("chrome://uw-command-bar.top-chrome/"), "--card"),
            "#171717");
  SaveWindowScreenshot(browser(), "command-bar-dark");
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

// Tab rows have no fill until the pointer is on them, and tiles and hovered
// rows are see-through, so the theme shows through all of them.
IN_PROC_BROWSER_TEST_F(TintBrowserTest, SidebarTabsLetTheThemeShowThrough) {
  for (int i = 0; i < 4; ++i) {
    ASSERT_TRUE(
        AddTabAtIndex(i + 1, GURL("about:blank"), ui::PAGE_TRANSITION_TYPED));
  }
  browser()->tab_strip_model()->ActivateTabAt(0);
  BrowserView* view = BrowserView::GetBrowserViewForBrowser(browser());
  const views::View* sidebar =
      view->vertical_tab_strip_region_view_for_testing();
  using State = TabStyle::TabSelectionState;
  Theme butter_to_sky;
  butter_to_sky.colors = {kButter, kSky};
  for (bool dark : {false, true}) {
    theme()->SetBrowserColorScheme(
        dark ? ThemeService::BrowserColorScheme::kDark
             : ThemeService::BrowserColorScheme::kLight);
    butter_to_sky.colors_in_dark_mode = dark;
    SetTheme(browser()->GetProfile(), butter_to_sky);
    const SkColor fill = SkColorSetA(SK_ColorWHITE, dark ? 0x14 : 0x75);
    EXPECT_EQ(SidebarTabFill(sidebar, false, State::kInactive, 0),
              SK_ColorTRANSPARENT);
    EXPECT_EQ(SidebarTabFill(sidebar, false, State::kInactive, 1), fill);
    EXPECT_EQ(SidebarTabFill(sidebar, true, State::kInactive, 0), fill);
    EXPECT_GT(SkColorGetA(*SidebarTabFill(sidebar, true, State::kInactive, 1)),
              SkColorGetA(fill));
    EXPECT_EQ(SidebarTabFill(sidebar, false, State::kActive, 1), std::nullopt);
    EXPECT_EQ(Color(kColorNewTabButtonCRBackgroundFrameActive), fill);

    const SkBitmap window = CaptureWindow(browser());
    const gfx::Rect bounds = view->GetWidget()->GetWindowBoundsInScreen();
    const float scale = static_cast<float>(window.width()) / bounds.width();
    const auto color_at = [&](int x, int y) {
      return window.getColor(base::ClampRound((x - bounds.x()) * scale),
                             base::ClampRound((y - bounds.y()) * scale));
    };
    std::vector<gfx::Rect> rows;
    const std::function<void(const views::View*)> visit =
        [&](const views::View* row) {
          if (std::string_view(row->GetClassName()) == "TabView") {
            rows.push_back(row->GetBoundsInScreen());
          }
          for (const views::View* child : row->children()) {
            visit(child);
          }
        };
    visit(sidebar);
    ASSERT_EQ(rows.size(), 5u);
    std::ranges::sort(rows, {}, &gfx::Rect::y);
    // The active tab is at the top. The four under it are at rest.
    for (size_t i = 1; i < rows.size(); ++i) {
      const int middle = rows[i].CenterPoint().y();
      EXPECT_LE(Gap(color_at(rows[i].x() + 4, middle),
                    color_at(rows[i].x() - 4, middle)),
                2)
          << "dark=" << dark << " row " << i;
    }
    SaveWindowScreenshot(browser(), dark ? "sidebar-dark" : "sidebar-light");
  }
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
  const SkBitmap smooth = CaptureWindow(browser());
  const int middle = smooth.height() / 2;
  EXPECT_LE(Roughness(smooth, 20, middle), 2);

  Theme grainy;
  grainy.grain = kFullGrain;
  SetTheme(browser()->GetProfile(), grainy);
  const SkBitmap rough = CaptureWindow(browser());
  EXPECT_GE(Roughness(rough, 20, middle), 8);
  EXPECT_LE(Roughness(rough, rough.width() * 3 / 5, middle), 1);
  SaveWindowScreenshot(browser(), "theme-grain");
}

// A dark window has no room to get darker, so grain lightens it instead.
IN_PROC_BROWSER_TEST_F(TintBrowserTest, GrainShowsOnADarkWindow) {
  theme()->SetBrowserColorScheme(ThemeService::BrowserColorScheme::kDark);
  Theme grainy;
  grainy.grain = kFullGrain;
  for (bool colors_in_dark_mode : {false, true}) {
    grainy.colors_in_dark_mode = colors_in_dark_mode;
    SetTheme(browser()->GetProfile(), grainy);
    const SkBitmap window = CaptureWindow(browser());
    const int middle = window.height() / 2;
    EXPECT_GE(Roughness(window, 20, middle), 8) << colors_in_dark_mode;
    EXPECT_LE(Roughness(window, window.width() * 3 / 5, middle), 1)
        << colors_in_dark_mode;
  }
  SaveWindowScreenshot(browser(), "theme-grain-dark");
}

IN_PROC_BROWSER_TEST_F(TintBrowserTest, GlassFrameIsOffUnlessTurnedOn) {
  EXPECT_FALSE(
      g_browser_process->local_state()->GetBoolean(prefs::kGlassFrameEnabled));
}

}  // namespace
}  // namespace uw
