// Copyright 2026 The uw Authors. All rights reserved.

#include <algorithm>

#include "base/strings/stringprintf.h"
#include "base/test/run_until.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/headless/test/headless_mode_browsertest.h"
#include "chrome/browser/themes/theme_service.h"
#include "chrome/browser/themes/theme_service_factory.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/color/chrome_color_id.h"
#include "chrome/browser/ui/frame/window_frame_util.h"
#include "chrome/browser/ui/view_ids.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/multi_contents_view.h"
#include "chrome/browser/ui/views/frame/vertical_tab_strip_region_view.h"
#include "chrome/browser/ui/views/toolbar/toolbar_view.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/prefs/pref_service.h"
#include "content/public/test/browser_test.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/base/hit_test.h"
#include "ui/color/color_provider.h"
#include "ui/events/event.h"
#include "ui/events/event_utils.h"
#include "ui/gfx/color_utils.h"
#include "ui/gfx/geometry/point.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/test/button_test_api.h"
#include "ui/views/view.h"
#include "ui/views/view_utils.h"
#include "ui/views/widget/widget.h"
#include "ui/views/window/non_client_view.h"
#include "url/gurl.h"
#include "uw/browser/tab_organization/window_screenshot.h"
#include "uw/browser/ui/look/page_card.h"
#include "uw/browser/ui/look/page_space_prefs.h"
#include "uw/browser/workspaces/workspace_theme.h"
#include "uw/components/look/theme.h"
#include "uw/components/look/tint.h"

namespace uw {
namespace {

// Windows' own buttons are 45px wide and 1px apart, as Chromium draws them.
constexpr int kOwnButtonWidth =
    WindowFrameUtil::kWindowsCaptionButtonWidth +
    WindowFrameUtil::kWindowsCaptionButtonVisualSpacing;

// A page that paints `color` behind its content.
GURL PageIn(SkColor color) {
  return GURL(base::StringPrintf(
      "data:text/html,<body style='background:%%23%02X%02X%02X'>",
      SkColorGetR(color), SkColorGetG(color), SkColorGetB(color)));
}

// On Windows, minimize, maximize, and close are toolbar buttons at the
// toolbar's end. See page_card.h.
class WindowButtonsWinBrowserTest : public headless::HeadlessModeBrowserTest {
 protected:
  // Browser tests on Windows draw nothing unless asked to, and one saves a
  // screenshot.
  WindowButtonsWinBrowserTest() { EnablePixelOutput(); }

  void SetUpOnMainThread() override {
    HeadlessModeBrowserTest::SetUpOnMainThread();
    widget()->SetBounds(gfx::Rect(0, 0, 1280, 900));
    ASSERT_TRUE(base::test::RunUntil(
        [&] { return InWidget(view()->toolbar()).width() > 0; }));
  }

  BrowserView* view() {
    return BrowserView::GetBrowserViewForBrowser(browser());
  }
  views::Widget* widget() { return view()->GetWidget(); }

  views::View* Button(int id) {
    return widget()->GetRootView()->GetViewByID(id);
  }

  static gfx::Rect InWidget(const views::View* view) {
    return view->ConvertRectToWidget(view->GetLocalBounds());
  }

  int HitTest(const gfx::Point& point) {
    return widget()->non_client_view()->frame_view()->NonClientHitTest(point);
  }

  void Press(int id) {
    views::test::ButtonTestApi(views::AsViewClass<views::Button>(Button(id)))
        .NotifyClick(ui::MouseEvent(ui::EventType::kMousePressed, gfx::Point(),
                                    gfx::Point(), ui::EventTimeForNow(),
                                    ui::EF_LEFT_MOUSE_BUTTON, 0));
  }

  // Turns the page card's margin off and waits for Windows' own buttons.
  void TurnOffPageSpace() {
    g_browser_process->local_state()->SetBoolean(kPageSpacePref, false);
    ASSERT_TRUE(base::test::RunUntil([&] {
      return InWidget(Button(VIEW_ID_CLOSE_BUTTON)).width() == kOwnButtonWidth;
    }));
  }

  // The most contrast with `page` that a pixel of the symbol on the button
  // `id` has in `window`, a capture of the whole window.
  float SymbolContrast(const SkBitmap& window, int id, SkColor page) {
    const float scale =
        static_cast<float>(window.width()) / widget()->GetRootView()->width();
    gfx::Rect symbol(InWidget(Button(id)).CenterPoint(), gfx::Size());
    symbol.Outset(8);
    symbol = gfx::ScaleToEnclosedRect(symbol, scale);
    float most = 1;
    for (int y = symbol.y(); y < symbol.bottom(); ++y) {
      for (int x = symbol.x(); x < symbol.right(); ++x) {
        most = std::max(
            most, color_utils::GetContrastRatio(window.getColor(x, y), page));
      }
    }
    return most;
  }
};

// The toolbar spans the page card from the page's own left edge, so the card's
// top corners are round, and the buttons sit inside its end.
IN_PROC_BROWSER_TEST_F(WindowButtonsWinBrowserTest,
                       TheButtonsSitAtTheEndOfAToolbarAsWideAsThePage) {
  const gfx::Rect toolbar = InWidget(view()->toolbar());
  const gfx::Rect page = InWidget(view()->multi_contents_view());
  EXPECT_EQ(toolbar.x(), page.x());
  EXPECT_EQ(toolbar.right(), page.right());

  for (int id : {VIEW_ID_MINIMIZE_BUTTON, VIEW_ID_MAXIMIZE_BUTTON,
                 VIEW_ID_CLOSE_BUTTON}) {
    ASSERT_TRUE(Button(id)) << id;
    EXPECT_TRUE(toolbar.Contains(InWidget(Button(id)))) << id;
  }
  EXPECT_EQ(toolbar.right() - InWidget(Button(VIEW_ID_CLOSE_BUTTON)).right(),
            kWindowButtonsEnd);
}

// A press on a button is that button's, and a press on the sidebar's empty
// space moves the window.
IN_PROC_BROWSER_TEST_F(WindowButtonsWinBrowserTest,
                       PressesGoToTheButtonsAndTheSidebarMovesTheWindow) {
  EXPECT_EQ(HitTest(InWidget(Button(VIEW_ID_MINIMIZE_BUTTON)).CenterPoint()),
            HTMINBUTTON);
  EXPECT_EQ(HitTest(InWidget(Button(VIEW_ID_MAXIMIZE_BUTTON)).CenterPoint()),
            HTMAXBUTTON);
  EXPECT_EQ(HitTest(InWidget(Button(VIEW_ID_CLOSE_BUTTON)).CenterPoint()),
            HTCLOSE);

  const gfx::Rect sidebar =
      InWidget(view()->vertical_tab_strip_region_view_for_testing());
  EXPECT_EQ(HitTest(sidebar.CenterPoint()), HTCAPTION);
}

// Without the page card's margin, Windows' own buttons sit at the window's
// top right, over the toolbar's end and as tall as the toolbar, and the
// toolbar's buttons end where they start. Turning the margin back on brings
// uw's buttons back. See docs/plans/arc-style-look.md#margin.
IN_PROC_BROWSER_TEST_F(WindowButtonsWinBrowserTest,
                       WithoutSpaceWindowsOwnButtonsSitAtTheTopRight) {
  TurnOffPageSpace();
  views::View* const close = Button(VIEW_ID_CLOSE_BUTTON);

  const gfx::Rect toolbar = InWidget(view()->toolbar());
  EXPECT_EQ(toolbar.right(), InWidget(view()).right());
  EXPECT_EQ(InWidget(close).right(), toolbar.right());
  for (int id : {VIEW_ID_MINIMIZE_BUTTON, VIEW_ID_MAXIMIZE_BUTTON,
                 VIEW_ID_CLOSE_BUTTON}) {
    const gfx::Rect button = InWidget(Button(id));
    EXPECT_EQ(button.y(), toolbar.y()) << id;
    EXPECT_EQ(button.height(), toolbar.height()) << id;
  }
  EXPECT_LE(InWidget(Button(VIEW_ID_APP_MENU)).right(),
            InWidget(Button(VIEW_ID_MINIMIZE_BUTTON)).x());
  EXPECT_FALSE(close->parent()->children().front()->GetVisible());
  EXPECT_EQ(HitTest(InWidget(Button(VIEW_ID_MAXIMIZE_BUTTON)).CenterPoint()),
            HTMAXBUTTON);
  EXPECT_EQ(HitTest(InWidget(close).CenterPoint()), HTCLOSE);
  SaveWindowScreenshot(browser(), "windows-no-margin");

  g_browser_process->local_state()->SetBoolean(kPageSpacePref, true);
  ASSERT_TRUE(base::test::RunUntil([&] {
    return InWidget(close).width() == kWindowButtonWidth + kWindowButtonGap;
  }));
  EXPECT_EQ(InWidget(view()->toolbar()).right() - InWidget(close).right(),
            kWindowButtonsEnd);
  EXPECT_TRUE(close->parent()->children().front()->GetVisible());
}

// Without the margin, Windows' own buttons show on a black page and on a white
// one, in a light theme in dark mode, where the theme and the toolbar differ.
// The page changes while the pointer is away, so the buttons repaint for it.
IN_PROC_BROWSER_TEST_F(WindowButtonsWinBrowserTest,
                       WithoutSpaceTheButtonsShowOnEveryPage) {
  SetThemeOfWindow(browser(), Theme());
  ThemeServiceFactory::GetForProfile(browser()->GetProfile())
      ->SetBrowserColorScheme(ThemeService::BrowserColorScheme::kDark);
  TurnOffPageSpace();
  for (SkColor page : {SK_ColorBLACK, SK_ColorWHITE}) {
    ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), PageIn(page)));
    ASSERT_TRUE(base::test::RunUntil([&] {
      return view()->GetColorProvider()->GetColor(kColorToolbar) == page;
    }));
    const SkBitmap window = CaptureWindow(browser());
    for (int id : {VIEW_ID_MINIMIZE_BUTTON, VIEW_ID_MAXIMIZE_BUTTON,
                   VIEW_ID_CLOSE_BUTTON}) {
      EXPECT_GE(SymbolContrast(window, id, page), kReadableContrast)
          << id << " on " << std::hex << page;
    }
    SaveBitmap(window, page == SK_ColorBLACK ? "windows-no-margin-black-page"
                                             : "windows-no-margin-white-page");
  }
}

// Run in the Windows VM with --run-manual --headful-mode. In a real window,
// maximize, restore, and minimize work from Windows' own buttons with the
// margin off.
IN_PROC_BROWSER_TEST_F(WindowButtonsWinBrowserTest,
                       MANUAL_WithoutSpaceTheButtonsWorkInARealWindow) {
  TurnOffPageSpace();
  SaveWindowScreenshot(browser(), "windows-no-margin-real");
  Press(VIEW_ID_MAXIMIZE_BUTTON);
  ASSERT_TRUE(base::test::RunUntil([&] {
    return widget()->IsMaximized() &&
           Button(VIEW_ID_RESTORE_BUTTON)->GetVisible();
  }));
  SaveWindowScreenshot(browser(), "windows-no-margin-maximized");
  Press(VIEW_ID_RESTORE_BUTTON);
  ASSERT_TRUE(base::test::RunUntil([&] { return !widget()->IsMaximized(); }));
  Press(VIEW_ID_MINIMIZE_BUTTON);
  ASSERT_TRUE(base::test::RunUntil([&] { return widget()->IsMinimized(); }));
}

}  // namespace
}  // namespace uw
