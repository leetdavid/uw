// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/browser/ui/views/page_colors/page_colors.h"

#include <algorithm>
#include <string>

#include "base/strings/stringprintf.h"
#include "base/test/run_until.h"
#include "chrome/browser/headless/test/headless_mode_browsertest.h"
#include "chrome/browser/themes/theme_service.h"
#include "chrome/browser/themes/theme_service_factory.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/color/chrome_color_id.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/top_container_view.h"
#include "chrome/test/base/ui_test_utils.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/base/page_transition_types.h"
#include "ui/color/color_id.h"
#include "ui/color/color_provider.h"
#include "ui/compositor/layer.h"
#include "ui/gfx/color_utils.h"
#include "ui/gfx/scoped_animation_duration_scale_mode.h"
#include "url/gurl.h"
#include "uw/browser/tab_organization/window_screenshot.h"
#include "uw/browser/ui/look/page_color.h"
#include "uw/components/look/tint.h"

namespace uw {
namespace {

constexpr SkColor kRed = SkColorSetRGB(0xC6, 0x28, 0x28);
constexpr SkColor kBlue = SkColorSetRGB(0x1A, 0x4F, 0xD6);

// A page that paints `color` behind its content.
GURL PageIn(SkColor color) {
  return GURL(base::StringPrintf(
      "data:text/html,<body style='background:%%23%02X%02X%02X'><h1>Page</h1>",
      SkColorGetR(color), SkColorGetG(color), SkColorGetB(color)));
}

class PageColorsBrowserTest : public headless::HeadlessModeBrowserTest {
 protected:
  BrowserView* view() {
    return BrowserView::GetBrowserViewForBrowser(browser());
  }

  SkColor Color(ui::ColorId id) {
    return view()->GetColorProvider()->GetColor(id);
  }

  void SetMode(ThemeService::BrowserColorScheme mode) {
    ThemeServiceFactory::GetForProfile(browser()->GetProfile())
        ->SetBrowserColorScheme(mode);
  }

  // Waits until the toolbar is `color`.
  [[nodiscard]] bool ToolbarBecomes(SkColor color) {
    return base::test::RunUntil([&] { return Color(kColorToolbar) == color; });
  }
};

// A red page gives a red toolbar and bookmarks bar, with white text, in both
// modes. The theme around the card does not change.
IN_PROC_BROWSER_TEST_F(PageColorsBrowserTest, ARedPageGivesARedToolbar) {
  for (auto mode : {ThemeService::BrowserColorScheme::kLight,
                    ThemeService::BrowserColorScheme::kDark}) {
    SetMode(mode);
    const SkColor frame = Color(ui::kColorFrameActive);
    ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), PageIn(kRed)));
    ASSERT_TRUE(ToolbarBecomes(kRed));
    EXPECT_EQ(Color(kColorBookmarkBarBackground), kRed);
    EXPECT_EQ(Color(kColorPageFieldText), kLightTextOnTint);
    EXPECT_EQ(Color(kColorToolbarButtonIcon),
              color_utils::AlphaBlend(SK_ColorWHITE, kRed, 0.75f));
    EXPECT_EQ(Color(kColorOmniboxActionIcon), Color(kColorToolbarButtonIcon));
    EXPECT_GE(color_utils::GetContrastRatio(Color(kColorPageFieldText),
                                            Color(kColorLocationBarBackground)),
              kReadableContrast);
    EXPECT_EQ(Color(ui::kColorFrameActive), frame);
    SaveWindowScreenshot(browser(),
                         mode == ThemeService::BrowserColorScheme::kLight
                             ? "page-color-red-light"
                             : "page-color-red-dark");
    ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), GURL("about:blank")));
  }
}

// A page in today's toolbar color, such as the terminal's black, keeps today's
// toolbar exactly.
IN_PROC_BROWSER_TEST_F(PageColorsBrowserTest,
                       TheTerminalsBlackKeepsTheToolbar) {
  SetMode(ThemeService::BrowserColorScheme::kDark);
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), PageIn(kRed)));
  ASSERT_TRUE(ToolbarBecomes(kRed));
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), PageIn(kBlackCard)));
  ASSERT_TRUE(ToolbarBecomes(kBlackCard));
  EXPECT_EQ(Color(kColorLocationBarBackground), kBlackCardField);
}

// A white page in a dark window gives a white toolbar with near-black text,
// while the window stays black.
IN_PROC_BROWSER_TEST_F(PageColorsBrowserTest, AWhitePageInADarkWindow) {
  SetMode(ThemeService::BrowserColorScheme::kDark);
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), PageIn(SK_ColorWHITE)));
  ASSERT_TRUE(ToolbarBecomes(SK_ColorWHITE));
  EXPECT_EQ(Color(kColorPageFieldText), kDarkTextOnTint);
  EXPECT_GE(color_utils::GetContrastRatio(Color(kColorPageFieldText),
                                          Color(kColorLocationBarBackground)),
            kReadableContrast);
  EXPECT_EQ(Color(ui::kColorFrameActive), SK_ColorBLACK);
  SaveWindowScreenshot(browser(), "page-color-white-in-dark");
}

// Switching tabs fades the old toolbar out over the new one.
IN_PROC_BROWSER_TEST_F(PageColorsBrowserTest, SwitchingTabsCrossfades) {
  gfx::ScopedAnimationDurationScaleMode normal_speed(
      gfx::ScopedAnimationDurationScaleMode::NORMAL_DURATION);
  SetMode(ThemeService::BrowserColorScheme::kLight);
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), PageIn(kRed)));
  ASSERT_TRUE(ToolbarBecomes(kRed));
  ASSERT_TRUE(AddTabAtIndex(1, PageIn(kBlue), ui::PAGE_TRANSITION_TYPED));
  ASSERT_TRUE(ToolbarBecomes(kBlue));

  const ui::Layer* top = view()->top_container()->layer();
  ASSERT_TRUE(top);
  const size_t settled = top->parent()->children().size();
  browser()->tab_strip_model()->ActivateTabAt(0);
  top = view()->top_container()->layer();
  const auto& layers = top->parent()->children();
  const auto above = std::next(std::ranges::find(layers, top));
  ASSERT_NE(above, layers.end());
  EXPECT_EQ((*above)->GetTargetOpacity(), 0.f);
  EXPECT_EQ(Color(kColorToolbar), kRed);
  EXPECT_TRUE(base::test::RunUntil(
      [&] { return top->parent()->children().size() == settled; }));
}

// A page that changes its background changes the toolbar without a reload.
IN_PROC_BROWSER_TEST_F(PageColorsBrowserTest, APageThatChangesItsColor) {
  SetMode(ThemeService::BrowserColorScheme::kLight);
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), PageIn(kRed)));
  ASSERT_TRUE(ToolbarBecomes(kRed));
  ASSERT_TRUE(
      content::ExecJs(browser()->tab_strip_model()->GetActiveWebContents(),
                      "document.body.style.background = '#1a4fd6'"));
  EXPECT_TRUE(ToolbarBecomes(kBlue));
}

}  // namespace
}  // namespace uw
