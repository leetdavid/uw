// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#import <AppKit/AppKit.h>

#include "base/test/run_until.h"
#include "chrome/browser/headless/test/headless_mode_browsertest.h"
#include "chrome/browser/themes/theme_service.h"
#include "chrome/browser/themes/theme_service_factory.h"
#include "chrome/browser/ui/animation/browser_animation_controller.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/browser/ui/tabs/vertical_tab_strip_state_controller.h"
#include "chrome/browser/ui/views/animations/tab_strip_animations.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "content/public/test/browser_test.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/base/mojom/themes.mojom.h"
#include "ui/gfx/color_utils.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/views/widget/widget.h"
#include "uw/browser/tab_organization/window_screenshot.h"

namespace uw {
namespace {

using WindowButtonsBrowserTest = headless::HeadlessModeBrowserTest;

// The darkest color in the corner where the window buttons sit, left of the
// sidebar's own buttons. A capture holds what uw draws, not the buttons.
SkColor DarkestUnderTheButtons(const SkBitmap& window) {
  const gfx::Rect corner(0, 0, 80, 45);
  SkColor darkest = SK_ColorWHITE;
  for (int y = corner.y(); y < corner.bottom(); ++y) {
    for (int x = corner.x(); x < corner.right(); ++x) {
      const SkColor color = window.getColor(x, y);
      if (color_utils::GetRelativeLuminance(color) <
          color_utils::GetRelativeLuminance(darkest)) {
        darkest = color;
      }
    }
  }
  return darkest;
}

// macOS draws the buttons of an inactive window in pale gray, which a light
// tint all but hides, so uw puts a dot under each one then.
IN_PROC_BROWSER_TEST_F(WindowButtonsBrowserTest,
                       AnInactiveWindowHasDotsUnderItsButtonsOnALightTint) {
  ThemeServiceFactory::GetForProfile(browser()->GetProfile())
      ->SetUserColorAndBrowserColorVariant(
          SkColorSetRGB(0xF7, 0xE0, 0x8A),
          ui::mojom::BrowserColorVariant::kTonalSpot);
  views::Widget* widget =
      BrowserView::GetBrowserViewForBrowser(browser())->GetWidget();
  ASSERT_TRUE(widget->ShouldPaintAsActive());
  const SkColor active = DarkestUnderTheButtons(CaptureWindow(browser()));

  NSWindow* window =
      browser()->GetWindow()->GetNativeWindow().GetNativeNSWindow();
  browser()->GetWindow()->Deactivate();
  [window resignKeyWindow];
  ASSERT_TRUE(
      base::test::RunUntil([&] { return !widget->ShouldPaintAsActive(); }));
  const SkColor inactive = DarkestUnderTheButtons(CaptureWindow(browser()));
  SaveWindowScreenshot(browser(), "inactive_window_buttons");

  EXPECT_GT(color_utils::GetContrastRatio(active, inactive), 2.0f)
      << color_utils::SkColorToRgbaString(active) << " "
      << color_utils::SkColorToRgbaString(inactive);
}

// A hidden sidebar takes macOS's window buttons away, and its floating card
// draws uw's. Showing the sidebar brings macOS's back.
IN_PROC_BROWSER_TEST_F(WindowButtonsBrowserTest,
                       AHiddenSidebarHidesTheWindowButtons) {
  NSWindow* window =
      browser()->GetWindow()->GetNativeWindow().GetNativeNSWindow();
  const NSWindowButton kinds[] = {
      NSWindowCloseButton, NSWindowMiniaturizeButton, NSWindowZoomButton};
  for (NSWindowButton kind : kinds) {
    ASSERT_TRUE([window standardWindowButton:kind]);
    EXPECT_FALSE([window standardWindowButton:kind].hidden);
  }

  tabs::VerticalTabStripStateController* sidebar =
      tabs::VerticalTabStripStateController::From(browser());
  sidebar->RequestCollapse(true);
  ASSERT_TRUE(base::test::RunUntil([&] { return sidebar->IsCollapsed(); }));
  for (NSWindowButton kind : kinds) {
    EXPECT_TRUE([window standardWindowButton:kind].hidden);
  }

  // They come back once the sidebar has slid all the way in.
  sidebar->RequestCollapse(false);
  ASSERT_TRUE(base::test::RunUntil([&] {
    return !sidebar->IsCollapsed() &&
           !BrowserAnimationController::From(browser())->IsAnimating(
               TabStripAnimations::kVerticalTabStrip);
  }));
  for (NSWindowButton kind : kinds) {
    EXPECT_FALSE([window standardWindowButton:kind].hidden);
  }
}

}  // namespace
}  // namespace uw
