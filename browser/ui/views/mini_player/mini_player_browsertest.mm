// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/browser/ui/views/mini_player/mini_player.h"

#import <Cocoa/Cocoa.h>

#include <string>

#include "base/memory/raw_ptr.h"
#include "base/test/run_until.h"
#include "chrome/browser/headless/test/headless_mode_browsertest.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/test/base/ui_test_utils.h"
#include "content/public/browser/picture_in_picture_window_controller.h"
#include "content/public/browser/video_picture_in_picture_window_controller.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "ui/base/test/ui_controls.h"
#include "ui/compositor/layer.h"
#include "ui/events/event.h"
#include "ui/events/event_constants.h"
#include "ui/events/test/cocoa_test_event_utils.h"
#include "ui/gfx/geometry/rounded_corners_f.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/test/button_test_api.h"
#include "uw/browser/tab_organization/window_screenshot.h"
#include "uw/browser/ui/glass/glass_screenshot.h"
#include "uw/browser/ui/views/mini_player/mini_player_view.h"
#include "uw/browser/workspaces/workspace_theme.h"
#include "uw/components/look/theme.h"

namespace uw {
namespace {

ui::MouseEvent Mouse(ui::EventType type, const gfx::Point& point) {
  return ui::MouseEvent(type, point, point, base::TimeTicks::Now(),
                        ui::EF_LEFT_MOUSE_BUTTON, ui::EF_LEFT_MOUSE_BUTTON);
}

class MiniPlayerBrowserTest : public headless::HeadlessModeBrowserTest {
 public:
  void SetUpOnMainThread() override {
    HeadlessModeBrowserTest::SetUpOnMainThread();
    ASSERT_TRUE(embedded_test_server()->Start());
  }

 protected:
  content::WebContents* active() {
    return browser()->tab_strip_model()->GetActiveWebContents();
  }

  // Opens Chromium's picture-in-picture test page, a 27 second, 480 by 320
  // video with sound, and puts its video in a mini player.
  MiniPlayer* OpenMiniPlayer() {
    EXPECT_TRUE(ui_test_utils::NavigateToURL(
        browser(), embedded_test_server()->GetURL(
                       "/media/picture-in-picture/window-size.html")));
    video_ = active();
    EXPECT_EQ(true, content::EvalJs(video_, "enterPictureInPicture()"));
    content::VideoOverlayWindow* window =
        content::PictureInPictureWindowController::
            GetOrCreateVideoPictureInPictureController(video_)
                ->GetWindowForTesting();
    // uw's patch makes every video picture-in-picture window a mini player.
    auto* mini_player = static_cast<MiniPlayer*>(window);
    EXPECT_EQ("MiniPlayer", mini_player->GetName());
    EXPECT_TRUE(base::test::RunUntil([&] { return mini_player->IsVisible(); }));
    return mini_player;
  }

  void Play() {
    ASSERT_EQ(true, content::EvalJs(video_, "video.play().then(() => true)"));
  }

  void Click(MiniPlayer* mini_player, int id) {
    auto* button =
        static_cast<views::Button*>(mini_player->view()->GetViewByID(id));
    ASSERT_TRUE(button);
    ASSERT_TRUE(button->GetVisible());
    views::test::ButtonTestApi(button).NotifyClick(
        Mouse(ui::EventType::kMousePressed, gfx::Point()));
  }

  // Moves the real mouse over the mini player's window. macOS gives moves to
  // the window under the pointer, so ui_controls is told which one.
  void MoveMouse(MiniPlayer* mini_player, const gfx::Point& point) {
    ui_controls::EnableUIControls();
    base::RunLoop moved;
    ASSERT_TRUE(ui_controls::SendMouseMoveNotifyWhenDone(
        point.x(), point.y(), moved.QuitClosure(),
        mini_player->GetNativeWindow()));
    moved.Run();
  }

  void Press(ui_controls::MouseButtonState state) {
    base::RunLoop done;
    ASSERT_TRUE(ui_controls::SendMouseEventsNotifyWhenDone(
        ui_controls::LEFT, state, done.QuitClosure()));
    done.Run();
  }

  double Number(const std::string& script) {
    return content::EvalJs(video_, script).ExtractDouble();
  }

  bool Bool(const std::string& script) {
    return content::EvalJs(video_, script).ExtractBool();
  }

  raw_ptr<content::WebContents> video_ = nullptr;
};

IN_PROC_BROWSER_TEST_F(MiniPlayerBrowserTest,
                       PictureInPictureOpensARoundedMiniPlayer) {
  MiniPlayer* mini_player = OpenMiniPlayer();
  // The window keeps the video's 3:2 shape, to a point of rounding, with 12px
  // corners.
  const gfx::Rect bounds = mini_player->GetWindowBoundsInScreen();
  EXPECT_NEAR(bounds.width() * 2.0 / 3, bounds.height(), 2);
  EXPECT_EQ(gfx::RoundedCornersF(12),
            mini_player->view()->layer()->rounded_corner_radii());
  // At rest only the video shows.
  EXPECT_FALSE(mini_player->view()->controls_shown());
}

// The way macOS hands a window the pointer: to its content view.
IN_PROC_BROWSER_TEST_F(MiniPlayerBrowserTest, ThePointerShowsTheControls) {
  MiniPlayer* mini_player = OpenMiniPlayer();
  NSWindow* window = mini_player->GetNativeWindow().GetNativeNSWindow();
  const NSRect content = window.contentView.bounds;
  const NSPoint point =
      NSMakePoint(NSWidth(content) * 0.3, NSHeight(content) * 0.7);
  [window.contentView
      mouseMoved:cocoa_test_event_utils::MouseEventAtPointInWindow(
                     point, NSEventTypeMouseMoved, window, 0)];
  EXPECT_TRUE(base::test::RunUntil(
      [&] { return mini_player->view()->controls_shown(); }));
}

IN_PROC_BROWSER_TEST_F(MiniPlayerBrowserTest, TheControlsPlayPauseSeekAndMute) {
  MiniPlayer* mini_player = OpenMiniPlayer();
  Play();
  MiniPlayerView* view = mini_player->view();
  view->ShowControls(true);
  ASSERT_TRUE(base::test::RunUntil([&] {
    return view->GetViewByID(MiniPlayerView::kForwardButton)->GetVisible();
  }));

  Click(mini_player, MiniPlayerView::kPlayPauseButton);
  EXPECT_TRUE(base::test::RunUntil([&] { return Bool("video.paused"); }));

  Click(mini_player, MiniPlayerView::kForwardButton);
  EXPECT_TRUE(
      base::test::RunUntil([&] { return Number("video.currentTime") >= 9; }));
  Click(mini_player, MiniPlayerView::kReplayButton);
  EXPECT_TRUE(
      base::test::RunUntil([&] { return Number("video.currentTime") < 5; }));

  Click(mini_player, MiniPlayerView::kMuteButton);
  EXPECT_TRUE(base::test::RunUntil([&] { return Bool("video.muted"); }));
}

IN_PROC_BROWSER_TEST_F(MiniPlayerBrowserTest, DraggingTheProgressBarSeeks) {
  MiniPlayer* mini_player = OpenMiniPlayer();
  Play();
  MiniPlayerView* view = mini_player->view();
  view->ShowControls(true);
  views::View* bar = view->GetViewByID(MiniPlayerView::kProgressBar);
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return bar->GetVisible() && bar->width() > 0; }));
  const gfx::Point half(bar->width() / 2, bar->height() / 2);
  bar->OnMousePressed(Mouse(ui::EventType::kMousePressed, gfx::Point()));
  bar->OnMouseDragged(Mouse(ui::EventType::kMouseDragged, half));
  bar->OnMouseReleased(Mouse(ui::EventType::kMouseReleased, half));
  EXPECT_TRUE(base::test::RunUntil([&] {
    const double time = Number("video.currentTime");
    return time > 12 && time < 15;
  }));
}

IN_PROC_BROWSER_TEST_F(MiniPlayerBrowserTest,
                       CloseLeavesTheVideoPausedInItsPage) {
  MiniPlayer* mini_player = OpenMiniPlayer();
  Play();
  mini_player->view()->ShowControls(true);
  ASSERT_TRUE(base::test::RunUntil([&] {
    return mini_player->view()
        ->GetViewByID(MiniPlayerView::kPlayPauseButton)
        ->GetVisible();
  }));
  Click(mini_player, MiniPlayerView::kCloseButton);
  EXPECT_TRUE(
      base::test::RunUntil([&] { return !Bool("isInPictureInPicture()"); }));
  EXPECT_TRUE(Bool("video.paused"));
}

IN_PROC_BROWSER_TEST_F(MiniPlayerBrowserTest, BackToTabSelectsTheVideosTab) {
  MiniPlayer* mini_player = OpenMiniPlayer();
  chrome::AddTabAt(browser(), GURL("about:blank"), -1, true);
  ASSERT_NE(video_, active());
  mini_player->view()->ShowControls(true);
  Click(mini_player, MiniPlayerView::kBackToTabButton);
  EXPECT_TRUE(base::test::RunUntil([&] { return active() == video_; }));
  EXPECT_TRUE(
      base::test::RunUntil([&] { return !Bool("isInPictureInPicture()"); }));
}

IN_PROC_BROWSER_TEST_F(MiniPlayerBrowserTest, HoldingPlaysTheVideoAtTwice) {
  MiniPlayer* mini_player = OpenMiniPlayer();
  Play();
  ASSERT_TRUE(content::ExecJs(video_, "video.playbackRate = 1.5"));
  MiniPlayerView* view = mini_player->view();
  const gfx::Point center = view->GetLocalBounds().CenterPoint();
  view->OnMousePressed(Mouse(ui::EventType::kMousePressed, center));
  EXPECT_TRUE(base::test::RunUntil([&] { return view->fast(); }));
  EXPECT_TRUE(view->GetViewByID(MiniPlayerView::kFastLabel)->GetVisible());
  EXPECT_TRUE(
      base::test::RunUntil([&] { return Number("video.playbackRate") == 2; }));
  SaveWidgetScreenshot(mini_player, "mini_player_held");

  view->OnMouseReleased(Mouse(ui::EventType::kMouseReleased, center));
  EXPECT_FALSE(view->fast());
  EXPECT_FALSE(view->GetViewByID(MiniPlayerView::kFastLabel)->GetVisible());
  EXPECT_TRUE(base::test::RunUntil(
      [&] { return Number("video.playbackRate") == 1.5; }));
}

IN_PROC_BROWSER_TEST_F(MiniPlayerBrowserTest, DraggingTheVideoMovesTheWindow) {
  MiniPlayer* mini_player = OpenMiniPlayer();
  MiniPlayerView* view = mini_player->view();
  const gfx::Rect before = mini_player->GetWindowBoundsInScreen();
  const gfx::Point center = view->GetLocalBounds().CenterPoint();
  view->OnMousePressed(Mouse(ui::EventType::kMousePressed, center));
  view->OnMouseDragged(
      Mouse(ui::EventType::kMouseDragged, center + gfx::Vector2d(-40, -30)));
  view->OnMouseReleased(
      Mouse(ui::EventType::kMouseReleased, center + gfx::Vector2d(-40, -30)));
  EXPECT_EQ(before.origin() + gfx::Vector2d(-40, -30),
            mini_player->GetWindowBoundsInScreen().origin());
  // A drag is not a hold.
  EXPECT_FALSE(view->fast());
}

IN_PROC_BROWSER_TEST_F(MiniPlayerBrowserTest, TheMiniPlayerLooksLikeTheFrames) {
  MiniPlayer* mini_player = OpenMiniPlayer();
  Play();
  MiniPlayerView* view = mini_player->view();
  view->ShowControls(true);
  ASSERT_TRUE(base::test::RunUntil([&] {
    return view->GetViewByID(MiniPlayerView::kForwardButton)->GetVisible();
  }));
  SaveWidgetScreenshot(mini_player, "mini_player_flat");

  Theme theme = ThemeOfWindow(browser());
  theme.glass = 100;
  SetThemeOfWindow(browser(), theme);
  view->ShowControls(true);
  SaveWidgetScreenshot(mini_player, "mini_player_glass");
}

// A real window, for pnpm run window-tests: a real pointer brings the
// controls, holding plays 2×, a drag moves the window, and the window shows
// over the desktop with its corners, lit edge, and shadow.
IN_PROC_BROWSER_TEST_F(MiniPlayerBrowserTest,
                       MANUAL_ARealPointerWorksTheMiniPlayer) {
  MiniPlayer* mini_player = OpenMiniPlayer();
  Play();
  MiniPlayerView* view = mini_player->view();
  const gfx::Rect bounds = mini_player->GetWindowBoundsInScreen();
  // On the video, clear of every button.
  const gfx::Point video(bounds.x() + bounds.width() * 3 / 10,
                         bounds.y() + bounds.height() * 3 / 10);
  MoveMouse(mini_player, video);
  EXPECT_TRUE(base::test::RunUntil([&] { return view->controls_shown(); }));
  // The window fades in when it opens.
  NSWindow* window = mini_player->GetNativeWindow().GetNativeNSWindow();
  EXPECT_TRUE(base::test::RunUntil([&] { return window.alphaValue == 1; }));
  SaveNativeWindowOverDesktop(mini_player->GetNativeWindow(),
                              "mini_player_over_desktop");

  Press(ui_controls::DOWN);
  EXPECT_TRUE(base::test::RunUntil([&] { return view->fast(); }));
  EXPECT_TRUE(
      base::test::RunUntil([&] { return Number("video.playbackRate") == 2; }));
  Press(ui_controls::UP);
  EXPECT_TRUE(
      base::test::RunUntil([&] { return Number("video.playbackRate") == 1; }));

  Press(ui_controls::DOWN);
  MoveMouse(mini_player, video + gfx::Vector2d(-80, -50));
  Press(ui_controls::UP);
  EXPECT_TRUE(base::test::RunUntil([&] {
    return mini_player->GetWindowBoundsInScreen().origin() ==
           bounds.origin() + gfx::Vector2d(-80, -50);
  }));
  EXPECT_FALSE(view->fast());
}

}  // namespace
}  // namespace uw
