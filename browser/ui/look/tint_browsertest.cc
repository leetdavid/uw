// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/components/look/tint.h"

#include "base/numerics/safe_conversions.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/headless/test/headless_mode_browsertest.h"
#include "chrome/browser/themes/theme_service.h"
#include "chrome/browser/themes/theme_service_factory.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/color/chrome_color_id.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/vertical_tab_strip_region_view.h"
#include "chrome/common/pref_names.h"
#include "components/prefs/pref_service.h"
#include "content/public/test/browser_test.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/base/mojom/themes.mojom.h"
#include "ui/color/color_id.h"
#include "ui/color/color_provider.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/views/widget/widget.h"
#include "uw/browser/tab_organization/window_screenshot.h"

namespace uw {
namespace {

constexpr SkColor kButter = SkColorSetRGB(0xF7, 0xE0, 0x8A);
constexpr SkColor kWine = SkColorSetRGB(0x5A, 0x1F, 0x3A);

class TintBrowserTest : public headless::HeadlessModeBrowserTest {
 protected:
  ThemeService* theme() {
    return ThemeServiceFactory::GetForProfile(browser()->GetProfile());
  }

  SkColor Color(ui::ColorId id) {
    return BrowserView::GetBrowserViewForBrowser(browser())
        ->GetColorProvider()
        ->GetColor(id);
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
  EXPECT_EQ(Color(ui::kColorFrameActive), kDefaultDarkTint);
  EXPECT_EQ(Color(kColorTabForegroundInactiveFrameActive), kLightTextOnTint);
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
  theme()->SetBrowserColorScheme(ThemeService::BrowserColorScheme::kDark);
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
    const SkColor sidebar = window.getColor(seam - reach, y);
    const SkColor frame = window.getColor(seam + reach, y);
    EXPECT_NEAR(SkColorGetR(sidebar), SkColorGetR(frame), 3) << "at y=" << y;
    EXPECT_NEAR(SkColorGetG(sidebar), SkColorGetG(frame), 3) << "at y=" << y;
    EXPECT_NEAR(SkColorGetB(sidebar), SkColorGetB(frame), 3) << "at y=" << y;
  }

  EXPECT_NE(window.getColor(seam + reach, inset),
            window.getColor(seam + reach, window.height() - inset));
}

IN_PROC_BROWSER_TEST_F(TintBrowserTest, GlassFrameIsOffUnlessTurnedOn) {
  EXPECT_FALSE(
      g_browser_process->local_state()->GetBoolean(prefs::kGlassFrameEnabled));
}

}  // namespace
}  // namespace uw
