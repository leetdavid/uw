// Copyright 2026 The uw Authors. All rights reserved.

#include "base/test/run_until.h"
#include "chrome/browser/headless/test/headless_mode_browsertest.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/view_ids.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/multi_contents_view.h"
#include "chrome/browser/ui/views/frame/vertical_tab_strip_region_view.h"
#include "chrome/browser/ui/views/toolbar/toolbar_view.h"
#include "content/public/test/browser_test.h"
#include "ui/base/hit_test.h"
#include "ui/gfx/geometry/point.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/views/view.h"
#include "ui/views/widget/widget.h"
#include "ui/views/window/non_client_view.h"
#include "uw/browser/ui/look/page_card.h"

namespace uw {
namespace {

// On Windows, minimize, maximize, and close are toolbar buttons at the
// toolbar's end. See page_card.h.
class WindowButtonsWinBrowserTest : public headless::HeadlessModeBrowserTest {
 protected:
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

}  // namespace
}  // namespace uw
