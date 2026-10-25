// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/browser/ui/views/mini_player/mini_player.h"

#import <Cocoa/Cocoa.h>

#include <string>

#include "base/command_line.h"
#include "base/memory/raw_ptr.h"
#include "base/run_loop.h"
#include "base/task/single_thread_task_runner.h"
#include "base/test/run_until.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/headless/test/headless_mode_browsertest.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/tabs/vertical_tab_strip_state_controller.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/media_session.h"
#include "content/public/browser/picture_in_picture_window_controller.h"
#include "content/public/browser/video_picture_in_picture_window_controller.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "media/base/media_switches.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "ui/base/test/ui_controls.h"
#include "ui/compositor/layer.h"
#include "ui/events/event.h"
#include "ui/events/event_constants.h"
#include "ui/events/test/cocoa_test_event_utils.h"
#include "ui/gfx/animation/animation.h"
#include "ui/gfx/geometry/rounded_corners_f.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/test/button_test_api.h"
#include "ui/views/test/views_test_utils.h"
#include "uw/browser/tab_organization/window_screenshot.h"
#include "uw/browser/ui/glass/glass_screenshot.h"
#include "uw/browser/ui/views/mini_player/app_activity.h"
#include "uw/browser/ui/views/mini_player/auto_mini_player.h"
#include "uw/browser/ui/views/mini_player/mini_player_dock.h"
#include "uw/browser/ui/views/mini_player/mini_player_placement.h"
#include "uw/browser/ui/views/mini_player/mini_player_prefs.h"
#include "uw/browser/ui/views/mini_player/mini_player_stack.h"
#include "uw/browser/ui/views/mini_player/mini_player_view.h"
#include "uw/browser/ui/views/mini_player/stack_row.h"
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
  void SetUpCommandLine(base::CommandLine* command_line) override {
    HeadlessModeBrowserTest::SetUpCommandLine(command_line);
    // The test video has sound. Nobody near the Mac should hear it.
    command_line->AppendSwitch(switches::kMuteAudio);
  }

  void SetUpOnMainThread() override {
    HeadlessModeBrowserTest::SetUpOnMainThread();
    ASSERT_TRUE(embedded_test_server()->Start());
    // uw is rarely the app in front while tests run. Most tests here look at
    // the floating mini player, which uw in the background shows.
    SetAppActiveForTesting(false);
  }

  void TearDownOnMainThread() override {
    SetAppActiveForTesting(std::nullopt);
    HeadlessModeBrowserTest::TearDownOnMainThread();
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

  bool Floats(MiniPlayer* mini_player) {
    return !mini_player->dock() && mini_player->views::Widget::IsVisible();
  }

  bool DocksIn(MiniPlayer* mini_player, BrowserWindowInterface* window) {
    return mini_player->dock() &&
           mini_player->dock() == MiniPlayerDock::From(window) &&
           !mini_player->views::Widget::IsVisible();
  }

  // Lets the mini player's placement settle, for checks that it stayed.
  void Settle() {
    base::RunLoop settled;
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
        FROM_HERE, settled.QuitClosure(), base::Milliseconds(400));
    settled.Run();
  }

  static PrefService* LocalState() { return g_browser_process->local_state(); }

  // Opens Chromium's picture-in-picture test page and plays its video with
  // sound, in the tab in front.
  void OpenPlayingVideo() {
    ASSERT_TRUE(ui_test_utils::NavigateToURL(
        browser(), embedded_test_server()->GetURL(
                       "/media/picture-in-picture/window-size.html")));
    video_ = active();
    Play();
    ASSERT_TRUE(base::test::RunUntil([&] {
      const media_session::mojom::MediaSessionInfoPtr info =
          content::MediaSession::Get(video_)->GetMediaSessionInfoSync();
      return info && info->playback_state ==
                         media_session::mojom::MediaPlaybackState::kPlaying;
    }));
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

  // Makes uw the app in front, with the test window as its active window.
  void BringUwToFront() {
    [NSApp activate];
    browser()->GetWindow()->Activate();
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

IN_PROC_BROWSER_TEST_F(MiniPlayerBrowserTest,
                       ItDocksInTheSidebarOfTheWindowInFront) {
  SetAppActiveForTesting(true);
  MiniPlayer* mini_player = OpenMiniPlayer();
  ASSERT_TRUE(
      base::test::RunUntil([&] { return DocksIn(mini_player, browser()); }));
  // The sidebar's width, the video's 3:2 shape, 10px corners.
  MiniPlayerView* view = mini_player->view();
  ASSERT_TRUE(base::test::RunUntil([&] { return view->width() > 0; }));
  EXPECT_NEAR(view->width() * 2.0 / 3, view->height(), 1);
  EXPECT_EQ(gfx::RoundedCornersF(10), view->layer()->rounded_corner_radii());
  EXPECT_EQ(mini_player->GetBounds(), view->GetBoundsInScreen());
  // It keeps playing there, and its controls work.
  Play();
  view->ShowControls(true);
  ASSERT_TRUE(base::test::RunUntil([&] {
    return view->GetViewByID(MiniPlayerView::kPlayPauseButton)->GetVisible();
  }));
  SaveWindowScreenshot(browser(), "mini_player_docked");
  Click(mini_player, MiniPlayerView::kPlayPauseButton);
  EXPECT_TRUE(base::test::RunUntil([&] { return Bool("video.paused"); }));
}

IN_PROC_BROWSER_TEST_F(MiniPlayerBrowserTest,
                       ItFloatsWhileUwIsInTheBackground) {
  SetAppActiveForTesting(true);
  MiniPlayer* mini_player = OpenMiniPlayer();
  ASSERT_TRUE(
      base::test::RunUntil([&] { return DocksIn(mini_player, browser()); }));
  SetAppActiveForTesting(false);
  EXPECT_TRUE(base::test::RunUntil([&] { return Floats(mini_player); }));
  EXPECT_EQ(gfx::RoundedCornersF(12),
            mini_player->view()->layer()->rounded_corner_radii());
  SetAppActiveForTesting(true);
  EXPECT_TRUE(
      base::test::RunUntil([&] { return DocksIn(mini_player, browser()); }));
}

IN_PROC_BROWSER_TEST_F(MiniPlayerBrowserTest,
                       WithFloatingInTheBackgroundOffItStaysDocked) {
  LocalState()->SetBoolean(kMiniPlayerFloatInBackgroundPref, false);
  SetAppActiveForTesting(true);
  MiniPlayer* mini_player = OpenMiniPlayer();
  ASSERT_TRUE(
      base::test::RunUntil([&] { return DocksIn(mini_player, browser()); }));
  SetAppActiveForTesting(false);
  Settle();
  EXPECT_TRUE(DocksIn(mini_player, browser()));
}

IN_PROC_BROWSER_TEST_F(MiniPlayerBrowserTest,
                       WithTheSidebarSwitchOffItAlwaysFloats) {
  LocalState()->SetBoolean(kMiniPlayerInSidebarPref, false);
  SetAppActiveForTesting(true);
  MiniPlayer* mini_player = OpenMiniPlayer();
  Settle();
  EXPECT_TRUE(Floats(mini_player));
  // Turning it on docks the video at once.
  LocalState()->SetBoolean(kMiniPlayerInSidebarPref, true);
  EXPECT_TRUE(
      base::test::RunUntil([&] { return DocksIn(mini_player, browser()); }));
}

IN_PROC_BROWSER_TEST_F(MiniPlayerBrowserTest, HidingTheSidebarFloatsIt) {
  SetAppActiveForTesting(true);
  MiniPlayer* mini_player = OpenMiniPlayer();
  ASSERT_TRUE(
      base::test::RunUntil([&] { return DocksIn(mini_player, browser()); }));
  auto* sidebar = tabs::VerticalTabStripStateController::From(browser());
  sidebar->RequestCollapse(true);
  EXPECT_TRUE(base::test::RunUntil([&] { return Floats(mini_player); }));
  sidebar->RequestCollapse(false);
  EXPECT_TRUE(
      base::test::RunUntil([&] { return DocksIn(mini_player, browser()); }));
}

IN_PROC_BROWSER_TEST_F(MiniPlayerBrowserTest, ItFollowsTheWindowInFront) {
  SetAppActiveForTesting(true);
  MiniPlayer* mini_player = OpenMiniPlayer();
  ASSERT_TRUE(
      base::test::RunUntil([&] { return DocksIn(mini_player, browser()); }));
  Browser* second = CreateBrowser(browser()->GetProfile());
  EXPECT_TRUE(
      base::test::RunUntil([&] { return DocksIn(mini_player, second); }));
  // Closing that window brings the video back to the first.
  CloseBrowserSynchronously(second);
  EXPECT_TRUE(
      base::test::RunUntil([&] { return DocksIn(mini_player, browser()); }));
  Play();
  EXPECT_FALSE(Bool("video.paused"));
}

IN_PROC_BROWSER_TEST_F(MiniPlayerBrowserTest, ClosingItEmptiesTheDock) {
  SetAppActiveForTesting(true);
  MiniPlayer* mini_player = OpenMiniPlayer();
  ASSERT_TRUE(
      base::test::RunUntil([&] { return DocksIn(mini_player, browser()); }));
  MiniPlayerDock* dock = MiniPlayerDock::From(browser());
  mini_player->view()->ShowControls(true);
  Click(mini_player, MiniPlayerView::kCloseButton);
  EXPECT_TRUE(
      base::test::RunUntil([&] { return !Bool("isInPictureInPicture()"); }));
  EXPECT_FALSE(dock->player());
  EXPECT_FALSE(dock->GetVisible());
  EXPECT_FALSE(mini_player->views::Widget::IsVisible());
}

// Opens the test page's video and plays it, in the tab in front.
IN_PROC_BROWSER_TEST_F(MiniPlayerBrowserTest,
                       LeavingAPlayingTabOpensItAndComingBackPutsItBack) {
  OpenPlayingVideo();
  chrome::AddTabAt(browser(), GURL("about:blank"), -1, true);
  EXPECT_TRUE(
      base::test::RunUntil([&] { return Bool("isInPictureInPicture()"); }));
  EXPECT_TRUE(AutoMiniPlayer::OpenedOnTabSwitch(video_));
  browser()->tab_strip_model()->ActivateTabAt(
      browser()->tab_strip_model()->GetIndexOfWebContents(video_));
  EXPECT_TRUE(
      base::test::RunUntil([&] { return !Bool("isInPictureInPicture()"); }));
  EXPECT_FALSE(Bool("video.paused"));
}

IN_PROC_BROWSER_TEST_F(MiniPlayerBrowserTest, LeavingAMutedVideoOpensNothing) {
  OpenPlayingVideo();
  ASSERT_TRUE(content::ExecJs(video_, "video.muted = true"));
  chrome::AddTabAt(browser(), GURL("about:blank"), -1, true);
  Settle();
  EXPECT_FALSE(Bool("isInPictureInPicture()"));
}

// Under 10,000 square pixels on the page, as Blink measures, a video stays.
IN_PROC_BROWSER_TEST_F(MiniPlayerBrowserTest, LeavingASmallVideoOpensNothing) {
  OpenPlayingVideo();
  ASSERT_TRUE(content::ExecJs(video_, "video.style.width = '80px';"
                                      "video.style.height = '53px';"));
  chrome::AddTabAt(browser(), GURL("about:blank"), -1, true);
  Settle();
  EXPECT_FALSE(Bool("isInPictureInPicture()"));
}

IN_PROC_BROWSER_TEST_F(MiniPlayerBrowserTest,
                       WithItsSwitchOffLeavingOpensNothing) {
  LocalState()->SetBoolean(kMiniPlayerOnTabSwitchPref, false);
  OpenPlayingVideo();
  chrome::AddTabAt(browser(), GURL("about:blank"), -1, true);
  Settle();
  EXPECT_FALSE(Bool("isInPictureInPicture()"));
}

IN_PROC_BROWSER_TEST_F(MiniPlayerBrowserTest,
                       AMiniPlayerTheUserOpenedStaysWhenTheyComeBack) {
  OpenMiniPlayer();
  Play();
  chrome::AddTabAt(browser(), GURL("about:blank"), -1, true);
  browser()->tab_strip_model()->ActivateTabAt(
      browser()->tab_strip_model()->GetIndexOfWebContents(video_));
  Settle();
  EXPECT_TRUE(Bool("isInPictureInPicture()"));
}

// A second video pauses the first and docks over it; the first waits in the
// stack, and a click on its row plays it again.
IN_PROC_BROWSER_TEST_F(MiniPlayerBrowserTest,
                       ASecondVideoPausesTheFirstAndStacksOnIt) {
  SetAppActiveForTesting(true);
  MiniPlayer* first = OpenMiniPlayer();
  Play();
  content::WebContents* first_video = video_;
  ASSERT_TRUE(base::test::RunUntil([&] { return DocksIn(first, browser()); }));
  // A still is copied every two seconds while the video plays.
  ASSERT_TRUE(
      base::test::RunUntil([&] { return !first->view()->still().isNull(); }));

  chrome::AddTabAt(browser(), GURL("about:blank"), -1, true);
  MiniPlayer* second = OpenMiniPlayer();
  ASSERT_NE(first, second);
  EXPECT_TRUE(base::test::RunUntil([&] {
    return content::EvalJs(first_video, "video.paused").ExtractBool();
  }));
  ASSERT_EQ(1u, MiniPlayerStack::Get().paused().size());
  EXPECT_EQ(first_video, MiniPlayerStack::Get().paused()[0].contents.get());

  MiniPlayerDock* dock = MiniPlayerDock::From(browser());
  ASSERT_TRUE(base::test::RunUntil([&] { return DocksIn(second, browser()); }));
  dock->OnMouseEntered(Mouse(ui::EventType::kMouseEntered, gfx::Point()));
  ASSERT_TRUE(dock->fanned());
  ASSERT_EQ(1u, dock->rows().size());
  ASSERT_TRUE(base::test::RunUntil([&] { return !dock->IsFanAnimating(); }));
  SaveWindowScreenshot(browser(), "mini_player_stack_fanned");
  views::test::ButtonTestApi(dock->rows()[0].get())
      .NotifyClick(Mouse(ui::EventType::kMousePressed, gfx::Point()));
  EXPECT_TRUE(base::test::RunUntil([&] {
    return content::EvalJs(first_video, "isInPictureInPicture()").ExtractBool();
  }));
  EXPECT_FALSE(content::EvalJs(first_video, "video.paused").ExtractBool());
  ASSERT_EQ(1u, MiniPlayerStack::Get().paused().size());
  EXPECT_EQ(video_, MiniPlayerStack::Get().paused()[0].contents.get());
}

// Under the pointer the paused videos rise from behind the playing one into
// rows; when it leaves they sink back to peeking.
IN_PROC_BROWSER_TEST_F(MiniPlayerBrowserTest, TheStackSlidesUpAndBackDown) {
  SetAppActiveForTesting(true);
  OpenMiniPlayer();
  chrome::AddTabAt(browser(), GURL("about:blank"), -1, true);
  MiniPlayer* second = OpenMiniPlayer();
  ASSERT_TRUE(base::test::RunUntil([&] { return DocksIn(second, browser()); }));
  MiniPlayerDock* dock = MiniPlayerDock::From(browser());
  ASSERT_TRUE(base::test::RunUntil([&] { return dock->rows().size() == 1; }));
  views::View* row = dock->rows()[0];
  EXPECT_FALSE(row->GetVisible());
  // At rest the paused video peeks 5px above the playing one.
  ASSERT_TRUE(base::test::RunUntil([&] { return dock->player()->y() == 5; }));

  dock->OnMouseEntered(Mouse(ui::EventType::kMouseEntered, gfx::Point()));
  EXPECT_TRUE(dock->IsFanAnimating() || gfx::Animation::PrefersReducedMotion());
  ASSERT_TRUE(base::test::RunUntil([&] { return !dock->IsFanAnimating(); }));
  views::test::RunScheduledLayout(dock->GetWidget());
  EXPECT_TRUE(row->GetVisible());
  EXPECT_EQ(1.0f, row->layer()->opacity());
  // The row sits 6px above the playing video, as wide as the dock.
  EXPECT_EQ(dock->player()->y() - 6, row->bounds().bottom());
  EXPECT_EQ(dock->width(), row->width());

  dock->OnMouseExited(Mouse(ui::EventType::kMouseExited, gfx::Point()));
  ASSERT_TRUE(base::test::RunUntil([&] { return !dock->IsFanAnimating(); }));
  views::test::RunScheduledLayout(dock->GetWidget());
  EXPECT_FALSE(row->GetVisible());
  EXPECT_EQ(5, dock->player()->y());
}

IN_PROC_BROWSER_TEST_F(MiniPlayerBrowserTest,
                       GoingToAPausedVideosTabTakesItOutOfTheStack) {
  SetAppActiveForTesting(true);
  OpenMiniPlayer();
  Play();
  content::WebContents* first_video = video_;
  chrome::AddTabAt(browser(), GURL("about:blank"), -1, true);
  OpenMiniPlayer();
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return MiniPlayerStack::Get().paused().size() == 1; }));
  browser()->tab_strip_model()->ActivateTabAt(
      browser()->tab_strip_model()->GetIndexOfWebContents(first_video));
  EXPECT_TRUE(MiniPlayerStack::Get().paused().empty());
}

IN_PROC_BROWSER_TEST_F(MiniPlayerBrowserTest, AFourthVideoDropsTheOldest) {
  std::vector<content::WebContents*> videos;
  for (int i = 0; i < 4; ++i) {
    if (i > 0) {
      chrome::AddTabAt(browser(), GURL("about:blank"), -1, true);
    }
    OpenMiniPlayer();
    videos.push_back(video_);
  }
  const std::vector<MiniPlayerStack::Entry>& paused =
      MiniPlayerStack::Get().paused();
  ASSERT_EQ(2u, paused.size());
  EXPECT_EQ(videos[2], paused[0].contents.get());
  EXPECT_EQ(videos[1], paused[1].contents.get());
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

// A real window, for pnpm run window-tests: with uw in front the video docks
// in the sidebar, behind another app it floats, and back in front it docks.
IN_PROC_BROWSER_TEST_F(MiniPlayerBrowserTest,
                       MANUAL_ItDocksInFrontAndFloatsBehindAnotherApp) {
  SetAppActiveForTesting(std::nullopt);
  BringUwToFront();
  ASSERT_TRUE(base::test::RunUntil([] { return IsAppActive(); }));
  MiniPlayer* mini_player = OpenMiniPlayer();
  Play();
  EXPECT_TRUE(
      base::test::RunUntil([&] { return DocksIn(mini_player, browser()); }));
  SaveNativeWindowOverDesktop(browser()->GetWindow()->GetNativeWindow(),
                              "mini_player_docked_real");

  NSRunningApplication* finder =
      [NSRunningApplication
          runningApplicationsWithBundleIdentifier:@"com.apple.finder"]
          .firstObject;
  ASSERT_TRUE(finder);
  [finder activateWithOptions:0];
  EXPECT_TRUE(base::test::RunUntil([&] { return Floats(mini_player); }));
  EXPECT_FALSE(Bool("video.paused"));

  BringUwToFront();
  EXPECT_TRUE(
      base::test::RunUntil([&] { return DocksIn(mini_player, browser()); }));
}

}  // namespace
}  // namespace uw
