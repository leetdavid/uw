// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/browser/ui/glass/glass.h"

#include <utility>

#include "base/strings/string_number_conversions.h"
#include "base/test/run_until.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/headless/test/headless_mode_browsertest.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "components/prefs/pref_service.h"
#include "content/public/test/browser_test.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/compositor/layer.h"
#include "ui/views/widget/widget.h"
#include "ui/views/window/non_client_view.h"
#include "uw/browser/tab_organization/window_screenshot.h"
#include "uw/browser/ui/glass/glass_prefs.h"
#include "uw/browser/workspaces/workspace_theme.h"
#include "uw/components/look/theme.h"

namespace uw {
namespace {

class GlassBrowserTest : public headless::HeadlessModeBrowserTest {
 protected:
  void SetLook(int transparency, int glass) {
    Theme theme = ThemeOfWindow(browser());
    theme.transparency = transparency;
    theme.glass = glass;
    SetThemeOfWindow(browser(), theme);
  }

  static bool MacCanShowGlass() {
    return CanShowGlass() && !ReducesTransparency();
  }
};

IN_PROC_BROWSER_TEST_F(GlassBrowserTest, ASolidThemeAsksForNoGlass) {
  EXPECT_EQ(ThemeOfWindow(browser()).transparency, 0);
  EXPECT_EQ(ThemeOfWindow(browser()).glass, 0);
  EXPECT_FALSE(WantsGlass(browser()));
  EXPECT_FALSE(ShowsGlass(browser()));
  EXPECT_EQ(CoverOfWindow(browser()), 1.0f);
  EXPECT_EQ(ToolbarCoverOfWindow(browser(), SK_ColorWHITE), 1.0f);
  EXPECT_EQ(ElementGlassOfWindow(browser()), 0);
}

IN_PROC_BROWSER_TEST_F(GlassBrowserTest, GlassAloneLeavesTheWindowSolid) {
  SetLook(0, 60);
  EXPECT_FALSE(WantsGlass(browser()));
  EXPECT_EQ(ElementGlassOfWindow(browser()), MacCanShowGlass() ? 60 : 0);

  SetLook(50, 60);
  EXPECT_EQ(WantsGlass(browser()), MacCanShowGlass());
}

IN_PROC_BROWSER_TEST_F(GlassBrowserTest,
                       WindowsBehindKeepGlassUntilTheSettingIsOff) {
  EXPECT_GT(MaxGlassWindows(), 1u);
  g_browser_process->local_state()->SetBoolean(kGlassBehindPref, false);
  EXPECT_EQ(MaxGlassWindows(), 1u);
}

IN_PROC_BROWSER_TEST_F(GlassBrowserTest, TransparencyLetsTheGlassThrough) {
  if (!MacCanShowGlass()) {
    GTEST_SKIP() << "This Mac draws no glass.";
  }
  browser()->GetWindow()->Activate();
  SetLook(kFullTransparency / 2, 0);
  ASSERT_TRUE(base::test::RunUntil([&] { return ShowsGlass(browser()); }));
  EXPECT_FLOAT_EQ(CoverOfWindow(browser()), 0.6f);
  EXPECT_FLOAT_EQ(ToolbarCoverOfWindow(browser(), SK_ColorWHITE), 0.6f);
  SaveWindowScreenshot(browser(), "transparency-half");

  SetLook(kFullTransparency, kFullGlass);
  EXPECT_FLOAT_EQ(CoverOfWindow(browser()), 0.2f);
  EXPECT_NEAR(ToolbarCoverOfWindow(browser(), SK_ColorWHITE), 0.52f, 0.011f);
  EXPECT_EQ(ElementGlassOfWindow(browser()), kFullGlass);

  SetLook(0, 0);
  ASSERT_TRUE(base::test::RunUntil([&] { return !ShowsGlass(browser()); }));
}

// The frame hears of the glass with the theme, so it never paints the old look
// under a toolbar that paints the new one.
IN_PROC_BROWSER_TEST_F(GlassBrowserTest, TheFrameTurnsToGlassWithTheTheme) {
  if (!MacCanShowGlass()) {
    GTEST_SKIP() << "This Mac draws no glass.";
  }
  browser()->GetWindow()->Activate();
  const ui::Layer* frame = BrowserView::GetBrowserViewForBrowser(browser())
                               ->GetWidget()
                               ->non_client_view()
                               ->frame_view()
                               ->layer();
  SetLook(kFullTransparency / 2, 0);
  ASSERT_TRUE(ShowsGlass(browser()));
  EXPECT_FALSE(frame->fills_bounds_opaquely());

  SetLook(0, 0);
  ASSERT_FALSE(ShowsGlass(browser()));
  EXPECT_TRUE(frame->fills_bounds_opaquely());
}

// Real windows only: macOS draws glass behind a window on screen. Saves how
// macOS shows the window, so the look can be checked by eye.
IN_PROC_BROWSER_TEST_F(GlassBrowserTest, MANUAL_GlassAsMacOSShowsIt) {
  if (!MacCanShowGlass()) {
    GTEST_SKIP() << "This Mac draws no glass.";
  }
  browser()->GetWindow()->Activate();
  for (const auto& [transparency, glass] :
       {std::pair{0, kFullGlass}, std::pair{kFullTransparency / 2, 40},
        std::pair{kFullTransparency, kFullGlass}}) {
    SetLook(transparency, glass);
    ASSERT_TRUE(base::test::RunUntil(
        [&] { return ShowsGlass(browser()) == (transparency > 0); }));
    CaptureWindow(browser());
    SaveNativeWindowScreenshot(
        browser(), "glass-native-" + base::NumberToString(transparency) + "-" +
                       base::NumberToString(glass));
  }

  Theme dark = ThemeOfWindow(browser());
  dark.transparency = kFullTransparency / 2;
  dark.glass = kFullGlass / 2;
  dark.mode = ThemeMode::kDark;
  SetThemeOfWindow(browser(), dark);
  CaptureWindow(browser());
  SaveNativeWindowScreenshot(browser(), "glass-native-dark");

  // A second window in front: the first keeps its glass, frosted.
  Browser* front = CreateBrowser(browser()->GetProfile());
  front->GetWindow()->Activate();
  ASSERT_TRUE(base::test::RunUntil([&] { return ShowsGlass(front); }));
  EXPECT_TRUE(ShowsGlass(browser()));
  CaptureWindow(front);
  SaveNativeWindowScreenshot(browser(), "glass-native-behind");
}

}  // namespace
}  // namespace uw
