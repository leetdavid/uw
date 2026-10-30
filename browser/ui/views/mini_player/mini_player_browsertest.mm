// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/browser/ui/views/mini_player/mini_player.h"

#import <Cocoa/Cocoa.h>

#include <algorithm>
#include <cmath>
#include <iterator>
#include <string>
#include <vector>

#include "base/command_line.h"
#include "base/files/file_path.h"
#include "base/memory/raw_ptr.h"
#include "base/process/launch.h"
#include "base/process/process.h"
#include "base/run_loop.h"
#include "base/strings/string_number_conversions.h"
#include "base/task/single_thread_task_runner.h"
#include "base/test/run_until.h"
#include "base/threading/thread_restrictions.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/headless/test/headless_mode_browsertest.h"
#include "chrome/browser/picture_in_picture/picture_in_picture_window_manager.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/color/chrome_color_id.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/tabs/vertical_tab_strip_state_controller.h"
#include "chrome/browser/ui/views/frame/browser_frame_view.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/browser_widget.h"
#include "chrome/grit/generated_resources.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/prefs/pref_service.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/document_picture_in_picture_window_controller.h"
#include "content/public/browser/media_session.h"
#include "content/public/browser/picture_in_picture_window_controller.h"
#include "content/public/browser/video_picture_in_picture_window_controller.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "media/base/media_switches.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/test/ui_controls.h"
#include "ui/color/color_id.h"
#include "ui/color/color_provider.h"
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
#include "uw/browser/ui/views/mini_player/dock_cover.h"
#include "uw/browser/ui/views/mini_player/live_pill.h"
#include "uw/browser/ui/views/mini_player/mini_player_dock.h"
#include "uw/browser/ui/views/mini_player/mini_player_placement.h"
#include "uw/browser/ui/views/mini_player/mini_player_prefs.h"
#include "uw/browser/ui/views/mini_player/mini_player_stack.h"
#include "uw/browser/ui/views/mini_player/mini_player_view.h"
#include "uw/browser/ui/views/mini_player/playback_controls.h"
#include "uw/browser/ui/views/mini_player/site_window.h"
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
    // the floating mini player, which uw in the background shows while other
    // apps cover it.
    SetAppActiveForTesting(false);
    SetCoveredPartForTesting(1);
  }

  void TearDownOnMainThread() override {
    SetAppActiveForTesting(std::nullopt);
    SetCoveredPartForTesting(std::nullopt);
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
  void Settle(base::TimeDelta delay = base::Milliseconds(400)) {
    base::RunLoop settled;
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
        FROM_HERE, settled.QuitClosure(), delay);
    settled.Run();
  }

  // Longer than a docked mini player must stay covered before it floats.
  static constexpr base::TimeDelta kPastTheHold = base::Milliseconds(1800);

  // Shows a plain window of another app, osascript, over `area` of the
  // screen, until the process ends.
  static base::Process CoverWith(const gfx::Rect& area) {
    // Cocoa's screen coordinates start at the bottom of the main screen.
    const int bottom =
        NSScreen.screens.firstObject.frame.size.height - area.bottom();
    base::CommandLine command(base::FilePath("/usr/bin/osascript"));
    for (const char* arg : {"-l", "JavaScript", "-e", R"(function run(argv) {
          ObjC.import('Cocoa');
          const [x, y, width, height] = argv.map(Number);
          const app = $.NSApplication.sharedApplication;
          app.setActivationPolicy(1);
          const window =
              $.NSWindow.alloc.initWithContentRectStyleMaskBackingDefer(
                  $.NSMakeRect(x, y, width, height), 0, 2, false);
          window.backgroundColor = $.NSColor.systemBlueColor;
          window.orderFrontRegardless;
          app.run;
        })"}) {
      command.AppendArg(arg);
    }
    for (int value : {area.x(), bottom, area.width(), area.height()}) {
      command.AppendArg(base::NumberToString(value));
    }
    return base::LaunchProcess(command, base::LaunchOptions());
  }

  static void End(base::Process& process) {
    base::ScopedAllowBlockingForTesting allow_blocking;
    process.Terminate(0, /*wait=*/true);
  }

  static PrefService* LocalState() { return g_browser_process->local_state(); }

  // The first view of class T in `root`.
  template <typename T>
  static T* Find(views::View* root) {
    std::vector<views::View*> views = {root};
    while (!views.empty()) {
      views::View* view = views.back();
      views.pop_back();
      if (auto* found = views::AsViewClass<T>(view)) {
        return found;
      }
      std::ranges::copy(view->children(), std::back_inserter(views));
    }
    return nullptr;
  }

  // The playback controls in the sidebar of `window`.
  static PlaybackControls* ControlsOf(Browser* window) {
    return Find<PlaybackControls>(
        BrowserView::GetBrowserViewForBrowser(window));
  }

  // Docks a site's window and then a video above it, in a tab of its own.
  // Returns the site's window.
  SiteWindow* SiteWindowUnderAVideo() {
    SetAppActiveForTesting(true);
    OpenSiteWindow(320, 200);
    SiteWindow* site_window = SiteWindow::Current();
    EXPECT_TRUE(base::test::RunUntil([&] { return SiteDocks(site_window); }));
    chrome::AddTabAt(browser(), GURL("about:blank"), -1, true);
    MiniPlayer* mini_player = OpenMiniPlayer();
    EXPECT_TRUE(
        base::test::RunUntil([&] { return DocksIn(mini_player, browser()); }));
    return site_window;
  }

  static bool SiteWindowWaits() {
    const std::vector<MiniPlayerStack::Entry>& paused =
        MiniPlayerStack::Get().paused();
    return paused.size() == 1 && !paused[0].play_site_window.is_null();
  }

  // Opens Chromium's document picture-in-picture test page in the tab in
  // front, and from it a site's own window of `width` by `height`. Returns
  // the tab.
  content::WebContents* OpenSiteWindow(int width, int height) {
    EXPECT_TRUE(ui_test_utils::NavigateToURL(
        browser(), embedded_test_server()->GetURL(
                       "/media/picture-in-picture/document-pip.html")));
    content::WebContents* opener = active();
    EXPECT_EQ(true,
              content::EvalJs(opener, content::JsReplace(
                                          "createDocumentPipWindow({width: $1, "
                                          "height: $2})",
                                          width, height)));
    EXPECT_TRUE(base::test::RunUntil([&] {
      return SiteWindow::Current() && SiteWindow::Current()->opener() == opener;
    }));
    return opener;
  }

  static int SiteWidth(content::WebContents* opener) {
    return content::EvalJs(opener, "pipWindow.innerWidth").ExtractInt();
  }

  static bool SiteWindowOpen(content::WebContents* opener) {
    return content::EvalJs(opener, "documentPictureInPicture.window !== null")
        .ExtractBool();
  }

  bool SiteDocks(SiteWindow* site_window) {
    return site_window->dock() == MiniPlayerDock::From(browser());
  }

  static bool SiteFloats(SiteWindow* site_window) {
    return !site_window->dock() && site_window->own_widget()->IsVisible();
  }

  static void SetMode(MiniPlayerMode mode) {
    LocalState()->SetInteger(kMiniPlayerModePref, static_cast<int>(mode));
  }

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

  // The button under `root` whose name is `message`, such as Pop out.
  static views::Button* ButtonNamed(views::View* root, int message) {
    const std::u16string name = l10n_util::GetStringUTF16(message);
    std::vector<views::View*> views = {root};
    while (!views.empty()) {
      views::View* view = views.back();
      views.pop_back();
      auto* button = views::AsViewClass<views::Button>(view);
      if (button && button->GetTooltipText() == name) {
        return button;
      }
      std::ranges::copy(view->children(), std::back_inserter(views));
    }
    return nullptr;
  }

  static void PressButton(views::Button* button) {
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

// With another app in front, it floats once that app's windows have covered
// it for a second, per T214.
IN_PROC_BROWSER_TEST_F(MiniPlayerBrowserTest, ItFloatsOnceAnotherAppCoversIt) {
  SetAppActiveForTesting(true);
  MiniPlayer* mini_player = OpenMiniPlayer();
  ASSERT_TRUE(
      base::test::RunUntil([&] { return DocksIn(mini_player, browser()); }));
  SetAppActiveForTesting(false);
  Settle();
  EXPECT_TRUE(DocksIn(mini_player, browser()));
  EXPECT_TRUE(base::test::RunUntil([&] { return Floats(mini_player); }));
  EXPECT_EQ(gfx::RoundedCornersF(12),
            mini_player->view()->layer()->rounded_corner_radii());
  SetAppActiveForTesting(true);
  EXPECT_TRUE(
      base::test::RunUntil([&] { return DocksIn(mini_player, browser()); }));
}

// Beside another app, with less than half of it covered, it stays docked.
// Once half is covered it floats, and it stays floating when uncovered, until
// uw is in front again.
IN_PROC_BROWSER_TEST_F(MiniPlayerBrowserTest, BesideAnotherAppItStaysDocked) {
  SetCoveredPartForTesting(0.4);
  SetAppActiveForTesting(true);
  MiniPlayer* mini_player = OpenMiniPlayer();
  ASSERT_TRUE(
      base::test::RunUntil([&] { return DocksIn(mini_player, browser()); }));
  SetAppActiveForTesting(false);
  Settle(kPastTheHold);
  EXPECT_TRUE(DocksIn(mini_player, browser()));

  SetCoveredPartForTesting(0.6);
  EXPECT_TRUE(base::test::RunUntil([&] { return Floats(mini_player); }));
  SetCoveredPartForTesting(0);
  Settle(kPastTheHold);
  EXPECT_TRUE(Floats(mini_player));

  SetAppActiveForTesting(true);
  EXPECT_TRUE(
      base::test::RunUntil([&] { return DocksIn(mini_player, browser()); }));
  SetAppActiveForTesting(false);
  Settle(kPastTheHold);
  EXPECT_TRUE(DocksIn(mini_player, browser()));
}

IN_PROC_BROWSER_TEST_F(MiniPlayerBrowserTest,
                       WithFloatingWhenCoveredOffItStaysDocked) {
  LocalState()->SetBoolean(kMiniPlayerFloatInBackgroundPref, false);
  SetAppActiveForTesting(true);
  MiniPlayer* mini_player = OpenMiniPlayer();
  ASSERT_TRUE(
      base::test::RunUntil([&] { return DocksIn(mini_player, browser()); }));
  SetAppActiveForTesting(false);
  Settle(kPastTheHold);
  EXPECT_TRUE(DocksIn(mini_player, browser()));
}

// With Don't dock, a video the user puts in picture in picture floats.
IN_PROC_BROWSER_TEST_F(MiniPlayerBrowserTest, WithDontDockItFloats) {
  SetMode(MiniPlayerMode::kDontDock);
  SetAppActiveForTesting(true);
  MiniPlayer* mini_player = OpenMiniPlayer();
  Settle();
  EXPECT_TRUE(Floats(mini_player));
  // Choosing Dock docks the video at once.
  SetMode(MiniPlayerMode::kDock);
  EXPECT_TRUE(
      base::test::RunUntil([&] { return DocksIn(mini_player, browser()); }));
}

IN_PROC_BROWSER_TEST_F(MiniPlayerBrowserTest,
                       WithDontDockLeavingAPlayingTabOpensNothing) {
  SetMode(MiniPlayerMode::kDontDock);
  OpenPlayingVideo();
  chrome::AddTabAt(browser(), GURL("about:blank"), -1, true);
  Settle();
  EXPECT_FALSE(Bool("isInPictureInPicture()"));
  EXPECT_FALSE(Bool("video.paused"));
}

// The stack lives only in the sidebar: without it, a second video leaves the
// first paused in its page.
IN_PROC_BROWSER_TEST_F(MiniPlayerBrowserTest,
                       WithDontDockASecondVideoLeavesTheFirstInItsPage) {
  SetMode(MiniPlayerMode::kDontDock);
  OpenMiniPlayer();
  Play();
  content::WebContents* first_video = video_;
  chrome::AddTabAt(browser(), GURL("about:blank"), -1, true);
  OpenMiniPlayer();
  EXPECT_TRUE(base::test::RunUntil([&] {
    return content::EvalJs(first_video, "video.paused").ExtractBool();
  }));
  EXPECT_TRUE(MiniPlayerStack::Get().paused().empty());
}

// With Off, nothing opens: not a video, nor a site's own window.
IN_PROC_BROWSER_TEST_F(MiniPlayerBrowserTest, WithOffNoPictureInPictureOpens) {
  SetMode(MiniPlayerMode::kOff);
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL(
                     "/media/picture-in-picture/window-size.html")));
  video_ = active();
  ASSERT_TRUE(content::ExecJs(
      video_, "enterPictureInPicture().then(() => 0, () => 0)"));
  Settle();
  EXPECT_FALSE(Bool("!!document.pictureInPictureElement"));

  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL(
                     "/media/picture-in-picture/document-pip.html")));
  const std::string open_window =
      "createDocumentPipWindow({width: 320, height: 200})"
      "    .then(() => 'opened', () => 'refused')";
  EXPECT_EQ("refused", content::EvalJs(active(), open_window));
  // Choosing Dock again lets it open.
  SetMode(MiniPlayerMode::kDock);
  EXPECT_EQ("opened", content::EvalJs(active(), open_window));
}

// The button just left of back to tab pops a docked video out, which stays
// out through app switches, and docks it again, per T215.
IN_PROC_BROWSER_TEST_F(MiniPlayerBrowserTest, ItPopsOutAndDocksByHand) {
  SetAppActiveForTesting(true);
  MiniPlayer* mini_player = OpenMiniPlayer();
  ASSERT_TRUE(
      base::test::RunUntil([&] { return DocksIn(mini_player, browser()); }));
  views::View* view = mini_player->view();
  EXPECT_EQ(view->GetViewByID(MiniPlayerView::kByHandButton),
            ButtonNamed(view, IDS_UW_MINI_PLAYER_POP_OUT));
  Click(mini_player, MiniPlayerView::kByHandButton);
  EXPECT_TRUE(base::test::RunUntil([&] { return Floats(mini_player); }));
  EXPECT_EQ(view->GetViewByID(MiniPlayerView::kByHandButton),
            ButtonNamed(view, IDS_UW_MINI_PLAYER_DOCK));

  SetAppActiveForTesting(false);
  Settle(kPastTheHold);
  SetAppActiveForTesting(true);
  Settle();
  EXPECT_TRUE(Floats(mini_player));

  Click(mini_player, MiniPlayerView::kByHandButton);
  EXPECT_TRUE(
      base::test::RunUntil([&] { return DocksIn(mini_player, browser()); }));
}

// Floating, the dock button shows only while the video could dock: not while
// the sidebar is hidden, nor with Don't dock.
IN_PROC_BROWSER_TEST_F(MiniPlayerBrowserTest,
                       TheDockButtonShowsOnlyWhenItCanDock) {
  SetAppActiveForTesting(true);
  MiniPlayer* mini_player = OpenMiniPlayer();
  ASSERT_TRUE(
      base::test::RunUntil([&] { return DocksIn(mini_player, browser()); }));
  Click(mini_player, MiniPlayerView::kByHandButton);
  ASSERT_TRUE(base::test::RunUntil([&] { return Floats(mini_player); }));
  views::View* by_hand =
      mini_player->view()->GetViewByID(MiniPlayerView::kByHandButton);
  EXPECT_TRUE(by_hand->GetVisible());

  auto* sidebar = tabs::VerticalTabStripStateController::From(browser());
  sidebar->RequestCollapse(true);
  EXPECT_TRUE(base::test::RunUntil([&] { return !by_hand->GetVisible(); }));
  sidebar->RequestCollapse(false);
  EXPECT_TRUE(base::test::RunUntil([&] { return by_hand->GetVisible(); }));
  EXPECT_TRUE(Floats(mini_player));

  SetMode(MiniPlayerMode::kDontDock);
  EXPECT_TRUE(base::test::RunUntil([&] { return !by_hand->GetVisible(); }));
}

// A docked call's pop out floats the call alone, under a video that stays
// docked; the dock in its floating bar docks it again.
IN_PROC_BROWSER_TEST_F(MiniPlayerBrowserTest,
                       ACallPopsOutAloneAndItsBarDocksIt) {
  SiteWindow* site_window = SiteWindowUnderAVideo();
  MiniPlayerDock* dock = MiniPlayerDock::From(browser());
  dock->site_window()->OnMouseEntered(
      Mouse(ui::EventType::kMouseEntered, gfx::Point()));
  views::Button* pop_out =
      ButtonNamed(dock->site_window(), IDS_UW_MINI_PLAYER_POP_OUT);
  ASSERT_TRUE(pop_out);
  PressButton(pop_out);
  EXPECT_TRUE(base::test::RunUntil([&] { return SiteFloats(site_window); }));
  Settle();
  EXPECT_TRUE(dock->player());

  views::Button* bar_dock = ButtonNamed(
      site_window->own_widget()->GetRootView(), IDS_UW_MINI_PLAYER_DOCK);
  ASSERT_TRUE(bar_dock);
  EXPECT_TRUE(base::test::RunUntil([&] { return bar_dock->GetVisible(); }));
  PressButton(bar_dock);
  EXPECT_TRUE(base::test::RunUntil([&] { return SiteDocks(site_window); }));
  EXPECT_TRUE(dock->player());
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
  // Until the new size is painted, Blink answers with the size it last
  // measured, so the tab switch waits for a frame painted after the change.
  ASSERT_TRUE(content::ExecJs(video_, R"(
    video.style.width = '80px';
    video.style.height = '53px';
    new Promise((resolve) =>
        requestAnimationFrame(() => requestAnimationFrame(resolve)));)"));
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

// Closing the playing video closes only it: the newest paused one comes up in
// its place, still paused.
IN_PROC_BROWSER_TEST_F(MiniPlayerBrowserTest,
                       ClosingThePlayingOneBringsUpTheNewestPaused) {
  SetAppActiveForTesting(true);
  MiniPlayer* first = OpenMiniPlayer();
  Play();
  content::WebContents* first_video = video_;
  ASSERT_TRUE(base::test::RunUntil([&] { return DocksIn(first, browser()); }));
  chrome::AddTabAt(browser(), GURL("about:blank"), -1, true);
  MiniPlayer* second = OpenMiniPlayer();
  ASSERT_TRUE(base::test::RunUntil([&] {
    return content::EvalJs(first_video, "video.paused").ExtractBool();
  }));
  ASSERT_TRUE(base::test::RunUntil([&] { return DocksIn(second, browser()); }));
  ASSERT_EQ(1u, MiniPlayerStack::Get().paused().size());

  second->view()->ShowControls(true);
  Click(second, MiniPlayerView::kCloseButton);
  EXPECT_TRUE(base::test::RunUntil([&] {
    return content::EvalJs(first_video, "isInPictureInPicture()").ExtractBool();
  }));
  EXPECT_TRUE(base::test::RunUntil([&] { return DocksIn(first, browser()); }));
  EXPECT_TRUE(content::EvalJs(first_video, "video.paused").ExtractBool());
  EXPECT_TRUE(MiniPlayerStack::Get().paused().empty());
  EXPECT_FALSE(Bool("isInPictureInPicture()"));
}

// A call's own window that takes the playing video's place keeps it: the
// video pauses into the stack, and no paused video comes back over the call.
IN_PROC_BROWSER_TEST_F(MiniPlayerBrowserTest,
                       ACallTakingTheVideosPlacePausesItIntoTheStack) {
  SetAppActiveForTesting(true);
  MiniPlayer* first = OpenMiniPlayer();
  Play();
  content::WebContents* first_video = video_;
  ASSERT_TRUE(base::test::RunUntil([&] { return DocksIn(first, browser()); }));
  chrome::AddTabAt(browser(), GURL("about:blank"), -1, true);
  MiniPlayer* second = OpenMiniPlayer();
  Play();
  content::WebContents* second_video = video_;
  ASSERT_TRUE(base::test::RunUntil([&] { return DocksIn(second, browser()); }));
  ASSERT_EQ(1u, MiniPlayerStack::Get().paused().size());

  chrome::AddTabAt(browser(), GURL("about:blank"), -1, true);
  OpenSiteWindow(320, 200);
  SiteWindow* call = SiteWindow::Current();
  ASSERT_TRUE(base::test::RunUntil([&] { return SiteDocks(call); }));
  Settle();
  EXPECT_TRUE(SiteDocks(call));
  EXPECT_FALSE(
      content::EvalJs(first_video, "isInPictureInPicture()").ExtractBool());
  EXPECT_TRUE(base::test::RunUntil([&] {
    return content::EvalJs(second_video, "video.paused").ExtractBool();
  }));
  const std::vector<MiniPlayerStack::Entry>& paused =
      MiniPlayerStack::Get().paused();
  ASSERT_EQ(2u, paused.size());
  EXPECT_EQ(second_video, paused[0].contents.get());
  EXPECT_EQ(first_video, paused[1].contents.get());
}

// The row's × under the pointer pauses the tab's sound and hides the row,
// which comes back when the tab plays again, per T223.
IN_PROC_BROWSER_TEST_F(MiniPlayerBrowserTest,
                       DismissingThePlaybackRowPausesAndHidesIt) {
  LocalState()->SetBoolean(kMiniPlayerOnTabSwitchPref, false);
  OpenPlayingVideo();
  PlaybackControls* controls = ControlsOf(browser());
  ASSERT_TRUE(controls);
  chrome::AddTabAt(browser(), GURL("about:blank"), -1, true);
  ASSERT_TRUE(base::test::RunUntil([&] { return controls->GetVisible(); }));
  ASSERT_TRUE(base::test::RunUntil([&] { return controls->playing(); }));
  EXPECT_FALSE(controls->dismiss()->GetVisible());
  controls->SetState(views::Button::STATE_HOVERED);
  ASSERT_TRUE(controls->dismiss()->GetVisible());
  SaveWindowScreenshot(browser(), "playback_controls_dismiss");

  PressButton(controls->dismiss());
  EXPECT_TRUE(base::test::RunUntil([&] { return Bool("video.paused"); }));
  EXPECT_FALSE(controls->GetVisible());
  Settle();
  EXPECT_FALSE(controls->GetVisible());

  Play();
  EXPECT_TRUE(base::test::RunUntil([&] { return controls->GetVisible(); }));
}

// A tab playing sound gets one row of playback controls while the user is on
// another tab. The row pauses and plays it, and a click goes to the tab.
IN_PROC_BROWSER_TEST_F(MiniPlayerBrowserTest,
                       ATabPlayingSoundGetsPlaybackControlsElsewhere) {
  LocalState()->SetBoolean(kMiniPlayerOnTabSwitchPref, false);
  OpenPlayingVideo();
  PlaybackControls* controls = ControlsOf(browser());
  ASSERT_TRUE(controls);
  ASSERT_TRUE(base::test::RunUntil([&] { return controls->tab() == video_; }));
  EXPECT_FALSE(controls->GetVisible());

  chrome::AddTabAt(browser(), GURL("about:blank"), -1, true);
  ASSERT_TRUE(base::test::RunUntil([&] { return controls->GetVisible(); }));
  ASSERT_TRUE(base::test::RunUntil([&] { return controls->playing(); }));
  // 6px above the foot, the row with Organize and the new button.
  views::test::RunScheduledLayout(controls->GetWidget());
  const auto& column = controls->parent()->children();
  const views::View* foot = *std::next(std::ranges::find(column, controls));
  EXPECT_EQ(6, foot->y() - controls->bounds().bottom());
  SaveWindowScreenshot(browser(), "playback_controls");
  views::test::ButtonTestApi(controls->play_pause())
      .NotifyClick(Mouse(ui::EventType::kMousePressed, gfx::Point()));
  EXPECT_TRUE(base::test::RunUntil([&] { return Bool("video.paused"); }));
  // Paused sound keeps its row.
  EXPECT_TRUE(base::test::RunUntil([&] { return !controls->playing(); }));
  EXPECT_TRUE(controls->GetVisible());
  SaveWindowScreenshot(browser(), "playback_controls_paused");
  views::test::ButtonTestApi(controls->play_pause())
      .NotifyClick(Mouse(ui::EventType::kMousePressed, gfx::Point()));
  EXPECT_TRUE(base::test::RunUntil([&] { return !Bool("video.paused"); }));

  views::test::ButtonTestApi(controls).NotifyClick(
      Mouse(ui::EventType::kMousePressed, gfx::Point()));
  EXPECT_EQ(video_, active());
  EXPECT_FALSE(controls->GetVisible());
}

// The pointer moving from the row onto its pause button stays on the row, so
// the row keeps its buttons instead of flickering.
IN_PROC_BROWSER_TEST_F(MiniPlayerBrowserTest,
                       ThePlaybackControlsStayUnderThePointerOnTheirButtons) {
  LocalState()->SetBoolean(kMiniPlayerOnTabSwitchPref, false);
  OpenPlayingVideo();
  chrome::AddTabAt(browser(), GURL("about:blank"), -1, true);
  PlaybackControls* controls = ControlsOf(browser());
  ASSERT_TRUE(base::test::RunUntil([&] { return controls->GetVisible(); }));
  views::test::RunScheduledLayout(controls->GetWidget());
  const auto move_to = [&](views::View* view) {
    gfx::Point point = view->GetLocalBounds().CenterPoint();
    views::View::ConvertPointToWidget(view, &point);
    ui::MouseEvent move(ui::EventType::kMouseMoved, point, point,
                        base::TimeTicks::Now(), 0, 0);
    controls->GetWidget()->OnMouseEvent(&move);
  };
  move_to(controls);
  ASSERT_EQ(views::Button::STATE_HOVERED, controls->GetState());
  move_to(controls->play_pause());
  EXPECT_EQ(views::Button::STATE_HOVERED, controls->GetState());
}

// Previous and next show whenever the site offers them, under the pointer or
// not, so the row never changes shape and pause never moves.
IN_PROC_BROWSER_TEST_F(MiniPlayerBrowserTest,
                       ThePlaybackControlsKeepTheirShapeUnderThePointer) {
  LocalState()->SetBoolean(kMiniPlayerOnTabSwitchPref, false);
  OpenPlayingVideo();
  ASSERT_TRUE(content::ExecJs(
      video_,
      "navigator.mediaSession.setActionHandler('previoustrack', () => {});"
      "navigator.mediaSession.setActionHandler('nexttrack', () => {});"));
  chrome::AddTabAt(browser(), GURL("about:blank"), -1, true);
  PlaybackControls* controls = ControlsOf(browser());
  ASSERT_TRUE(base::test::RunUntil([&] {
    return controls->GetVisible() && controls->next()->GetVisible();
  }));
  EXPECT_TRUE(controls->previous()->GetVisible());
  EXPECT_EQ(views::Button::STATE_NORMAL, controls->GetState());
  views::test::RunScheduledLayout(controls->GetWidget());
  const gfx::Rect pause = controls->play_pause()->bounds();
  gfx::Point point = controls->GetLocalBounds().CenterPoint();
  views::View::ConvertPointToWidget(controls, &point);
  ui::MouseEvent move(ui::EventType::kMouseMoved, point, point,
                      base::TimeTicks::Now(), 0, 0);
  controls->GetWidget()->OnMouseEvent(&move);
  ASSERT_EQ(views::Button::STATE_HOVERED, controls->GetState());
  views::test::RunScheduledLayout(controls->GetWidget());
  EXPECT_EQ(pause, controls->play_pause()->bounds());
}

// A site's artwork can come in RGBA order; the row shows it, in the order
// views draw.
IN_PROC_BROWSER_TEST_F(MiniPlayerBrowserTest,
                       ThePlaybackControlsTakeArtworkInAnyPixelOrder) {
  PlaybackControls* controls = ControlsOf(browser());
  SkBitmap art;
  ASSERT_TRUE(art.tryAllocPixels(
      SkImageInfo::Make(64, 64, kRGBA_8888_SkColorType, kPremul_SkAlphaType)));
  art.eraseColor(SK_ColorRED);
  controls->SetArtForTesting(art);
  EXPECT_TRUE(controls->has_art());
}

// Sound in a mini player has the mini player's controls, so no row.
IN_PROC_BROWSER_TEST_F(MiniPlayerBrowserTest,
                       SoundInAMiniPlayerGetsNoPlaybackControls) {
  OpenPlayingVideo();
  chrome::AddTabAt(browser(), GURL("about:blank"), -1, true);
  ASSERT_TRUE(
      base::test::RunUntil([&] { return Bool("isInPictureInPicture()"); }));
  Settle();
  EXPECT_FALSE(ControlsOf(browser())->GetVisible());
  // Closing the mini player leaves the video in its page: playing there, it
  // gets the row.
  PictureInPictureWindowManager::GetInstance()->ExitPictureInPicture();
  ASSERT_TRUE(content::ExecJs(video_, "video.play()"));
  EXPECT_TRUE(base::test::RunUntil(
      [&] { return ControlsOf(browser())->GetVisible(); }));
}

// A site's own small window, such as a call, docks at the bottom of the
// sidebar at its width, and floats in its own window behind another app.
IN_PROC_BROWSER_TEST_F(MiniPlayerBrowserTest, ASmallSiteWindowDocks) {
  SetAppActiveForTesting(true);
  content::WebContents* opener = OpenSiteWindow(320, 200);
  SiteWindow* site_window = SiteWindow::Current();
  ASSERT_TRUE(base::test::RunUntil([&] { return SiteDocks(site_window); }));
  EXPECT_FALSE(site_window->own_widget()->IsVisible());
  // As wide as the dock, in the shape the site asked for.
  MiniPlayerDock* dock = MiniPlayerDock::From(browser());
  views::test::RunScheduledLayout(dock->GetWidget());
  const int width = dock->width();
  EXPECT_TRUE(base::test::RunUntil([&] { return SiteWidth(opener) == width; }));
  EXPECT_EQ(static_cast<int>(std::lround(width * 200.0 / 320)),
            content::EvalJs(opener, "pipWindow.innerHeight"));
  SaveWindowScreenshot(browser(), "site_window_docked");

  SetAppActiveForTesting(false);
  EXPECT_TRUE(base::test::RunUntil([&] { return SiteFloats(site_window); }));
  EXPECT_TRUE(base::test::RunUntil([&] { return SiteWidth(opener) > width; }));
  EXPECT_TRUE(SiteWindowOpen(opener));
  SetAppActiveForTesting(true);
  EXPECT_TRUE(base::test::RunUntil([&] { return SiteDocks(site_window); }));
}

IN_PROC_BROWSER_TEST_F(MiniPlayerBrowserTest, ABigSiteWindowFloats) {
  SetAppActiveForTesting(true);
  OpenSiteWindow(600, 300);
  Settle();
  EXPECT_TRUE(SiteFloats(SiteWindow::Current()));
}

// A video docks above a site's window, which is live and keeps running.
// Behind another app both float, the video just above.
IN_PROC_BROWSER_TEST_F(MiniPlayerBrowserTest, AVideoDocksAboveASiteWindow) {
  SetAppActiveForTesting(true);
  content::WebContents* opener = OpenSiteWindow(320, 200);
  SiteWindow* site_window = SiteWindow::Current();
  ASSERT_TRUE(base::test::RunUntil([&] { return SiteDocks(site_window); }));
  chrome::AddTabAt(browser(), GURL("about:blank"), -1, true);
  MiniPlayer* mini_player = OpenMiniPlayer();
  ASSERT_TRUE(
      base::test::RunUntil([&] { return DocksIn(mini_player, browser()); }));
  EXPECT_TRUE(SiteWindowOpen(opener));
  EXPECT_EQ(site_window, SiteWindow::Current());
  EXPECT_TRUE(SiteDocks(site_window));
  MiniPlayerDock* dock = MiniPlayerDock::From(browser());
  views::test::RunScheduledLayout(dock->GetWidget());
  EXPECT_LT(dock->player()->bounds().bottom(), dock->site_window()->y());
  SaveWindowScreenshot(browser(), "site_window_under_video");
  dock->site_window()->OnMouseEntered(
      Mouse(ui::EventType::kMouseEntered, gfx::Point()));
  SaveWindowScreenshot(browser(), "site_window_controls");

  SetAppActiveForTesting(false);
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return Floats(mini_player) && SiteFloats(site_window); }));
  EXPECT_LE(mini_player->GetWindowBoundsInScreen().bottom(),
            site_window->own_widget()->GetWindowBoundsInScreen().y());

  // Closing the video leaves the site's window.
  PictureInPictureWindowManager::GetInstance()->ExitPictureInPicture();
  Settle();
  EXPECT_TRUE(SiteWindowOpen(opener));
  EXPECT_EQ(site_window, SiteWindow::Current());
}

// A site may grow its window only after a click in it; past the limit it
// floats, and keeps floating.
IN_PROC_BROWSER_TEST_F(MiniPlayerBrowserTest,
                       ASiteWindowGrownTooBigFloatsForGood) {
  SetAppActiveForTesting(true);
  OpenSiteWindow(320, 200);
  SiteWindow* site_window = SiteWindow::Current();
  ASSERT_TRUE(base::test::RunUntil([&] { return SiteDocks(site_window); }));
  ASSERT_TRUE(content::ExecJs(site_window->page(), "resizeTo(700, 520)"));
  EXPECT_TRUE(base::test::RunUntil([&] { return SiteFloats(site_window); }));
  SetAppActiveForTesting(false);
  Settle();
  SetAppActiveForTesting(true);
  Settle();
  EXPECT_TRUE(SiteFloats(site_window));
}

IN_PROC_BROWSER_TEST_F(MiniPlayerBrowserTest, ClosingADockedSiteWindow) {
  SetAppActiveForTesting(true);
  content::WebContents* opener = OpenSiteWindow(320, 200);
  ASSERT_TRUE(
      base::test::RunUntil([&] { return SiteDocks(SiteWindow::Current()); }));
  SiteWindow::Current()->Close();
  EXPECT_TRUE(base::test::RunUntil([&] { return !SiteWindowOpen(opener); }));
  EXPECT_TRUE(base::test::RunUntil([] { return !SiteWindow::Current(); }));
  EXPECT_FALSE(MiniPlayerDock::From(browser())->GetVisible());
}

IN_PROC_BROWSER_TEST_F(MiniPlayerBrowserTest, ASiteClosingItsDockedWindow) {
  SetAppActiveForTesting(true);
  content::WebContents* opener = OpenSiteWindow(320, 200);
  ASSERT_TRUE(
      base::test::RunUntil([&] { return SiteDocks(SiteWindow::Current()); }));
  ASSERT_TRUE(content::ExecJs(opener, "pipWindow.close()"));
  EXPECT_TRUE(base::test::RunUntil([&] { return !SiteWindowOpen(opener); }));
  EXPECT_TRUE(base::test::RunUntil([] { return !SiteWindow::Current(); }));
}

// One site's window at a time: a newer one takes its place.
IN_PROC_BROWSER_TEST_F(MiniPlayerBrowserTest, ANewerSiteWindowTakesItsPlace) {
  SetAppActiveForTesting(true);
  content::WebContents* first = OpenSiteWindow(320, 200);
  ASSERT_TRUE(
      base::test::RunUntil([&] { return SiteDocks(SiteWindow::Current()); }));
  chrome::AddTabAt(browser(), GURL("about:blank"), -1, true);
  content::WebContents* second = OpenSiteWindow(320, 240);
  EXPECT_TRUE(base::test::RunUntil([&] { return !SiteWindowOpen(first); }));
  EXPECT_TRUE(SiteWindowOpen(second));
  EXPECT_TRUE(
      base::test::RunUntil([&] { return SiteDocks(SiteWindow::Current()); }));
}

// A site's window that is not live pauses and waits in the stack under the
// video; played again from its row, it comes back and the video waits.
IN_PROC_BROWSER_TEST_F(MiniPlayerBrowserTest,
                       ANonLiveSiteWindowWaitsInTheStack) {
  SetAppActiveForTesting(true);
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL(
                     "/media/picture-in-picture/document-pip.html")));
  SetSiteLive(LocalState(), std::string(active()->GetLastCommittedURL().host()),
              false);
  content::WebContents* opener = OpenSiteWindow(320, 200);
  SiteWindow* site_window = SiteWindow::Current();
  ASSERT_FALSE(site_window->live());
  ASSERT_TRUE(base::test::RunUntil([&] { return SiteDocks(site_window); }));
  chrome::AddTabAt(browser(), GURL("about:blank"), -1, true);
  MiniPlayer* mini_player = OpenMiniPlayer();
  Play();
  content::WebContents* video = video_;
  ASSERT_TRUE(
      base::test::RunUntil([&] { return DocksIn(mini_player, browser()); }));
  EXPECT_TRUE(base::test::RunUntil([] { return SiteWindowWaits(); }));
  EXPECT_TRUE(site_window->waiting());
  EXPECT_FALSE(MiniPlayerDock::From(browser())->site_window());
  EXPECT_TRUE(SiteWindowOpen(opener));
  MiniPlayerDock* dock = MiniPlayerDock::From(browser());
  dock->OnMouseEntered(Mouse(ui::EventType::kMouseEntered, gfx::Point()));
  ASSERT_TRUE(base::test::RunUntil([&] { return !dock->IsFanAnimating(); }));
  SaveWindowScreenshot(browser(), "site_window_waiting");

  MiniPlayerStack::Get().Play(0);
  EXPECT_TRUE(base::test::RunUntil([&] { return SiteDocks(site_window); }));
  EXPECT_FALSE(site_window->waiting());
  EXPECT_TRUE(base::test::RunUntil([&] {
    const std::vector<MiniPlayerStack::Entry>& paused =
        MiniPlayerStack::Get().paused();
    return paused.size() == 1 && paused[0].contents.get() == video;
  }));
  EXPECT_TRUE(base::test::RunUntil(
      [&] { return content::EvalJs(video, "video.paused").ExtractBool(); }));
}

// The Live pill on the docked page turns the site's choice off, which sends
// the window to the stack under the video, and on, which brings it back.
IN_PROC_BROWSER_TEST_F(MiniPlayerBrowserTest, TheLivePillTurnsLiveOffAndOn) {
  SiteWindow* site_window = SiteWindowUnderAVideo();
  ASSERT_TRUE(site_window->live());
  MiniPlayerDock* dock = MiniPlayerDock::From(browser());
  dock->site_window()->OnMouseEntered(
      Mouse(ui::EventType::kMouseEntered, gfx::Point()));
  LivePill* pill = Find<LivePill>(dock->site_window());
  ASSERT_TRUE(pill);
  views::test::ButtonTestApi(pill).NotifyClick(
      Mouse(ui::EventType::kMousePressed, gfx::Point()));
  EXPECT_FALSE(IsSiteLive(LocalState(), site_window->site()));
  EXPECT_TRUE(base::test::RunUntil([] { return SiteWindowWaits(); }));

  SetSiteLive(LocalState(), site_window->site(), true);
  EXPECT_TRUE(base::test::RunUntil([&] { return SiteDocks(site_window); }));
  EXPECT_TRUE(MiniPlayerStack::Get().paused().empty());
}

// With the video closed, a site's window waiting under it plays again.
IN_PROC_BROWSER_TEST_F(MiniPlayerBrowserTest,
                       AWaitingSiteWindowComesBackWhenTheVideoCloses) {
  SiteWindow* site_window = SiteWindowUnderAVideo();
  SetSiteLive(LocalState(), site_window->site(), false);
  ASSERT_TRUE(base::test::RunUntil([] { return SiteWindowWaits(); }));
  PictureInPictureWindowManager::GetInstance()->ExitPictureInPicture();
  EXPECT_TRUE(base::test::RunUntil([&] { return SiteDocks(site_window); }));
  EXPECT_FALSE(site_window->waiting());
}

// Outside the sidebar there is no stack: a site's window that is not live
// closes when a video comes.
IN_PROC_BROWSER_TEST_F(MiniPlayerBrowserTest,
                       WithDontDockANonLiveSiteWindowCloses) {
  SetMode(MiniPlayerMode::kDontDock);
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL(
                     "/media/picture-in-picture/document-pip.html")));
  SetSiteLive(LocalState(), std::string(active()->GetLastCommittedURL().host()),
              false);
  content::WebContents* opener = OpenSiteWindow(320, 200);
  chrome::AddTabAt(browser(), GURL("about:blank"), -1, true);
  OpenMiniPlayer();
  EXPECT_TRUE(base::test::RunUntil([&] { return !SiteWindowOpen(opener); }));
  EXPECT_TRUE(MiniPlayerStack::Get().paused().empty());
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

// A site's own picture-in-picture window, such as a Meet call, floats with a
// 30px bar in the tint of the window it came from.
IN_PROC_BROWSER_TEST_F(MiniPlayerBrowserTest,
                       ASitesOwnWindowWearsItsOpenersTint) {
  Theme theme = ThemeOfWindow(browser());
  theme.colors = {SkColorSetRGB(0x3E, 0x7C, 0x6B)};
  SetThemeOfWindow(browser(), theme);
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL(
                     "/media/picture-in-picture/document-pip.html")));
  ASSERT_EQ(true, content::EvalJs(
                      active(),
                      "createDocumentPipWindow({width: 320, height: 200})"));
  content::WebContents* page =
      content::PictureInPictureWindowController::
          GetOrCreateDocumentPictureInPictureController(active())
              ->GetChildWebContents();
  ASSERT_TRUE(page);
  BrowserWindowInterface* window =
      tabs::TabInterface::GetFromContents(page)->GetBrowserWindowInterface();
  ASSERT_EQ(BrowserWindowInterface::TYPE_PICTURE_IN_PICTURE, window->GetType());
  EXPECT_EQ(theme, ThemeOfWindow(window));

  BrowserView* view = BrowserView::GetBrowserViewForNativeWindow(
      window->GetWindow()->GetNativeWindow());
  EXPECT_EQ(30, view->browser_widget()->GetFrameView()->GetTopInset(false));
  EXPECT_EQ(
      BrowserView::GetBrowserViewForBrowser(browser())
          ->GetColorProvider()
          ->GetColor(ui::kColorFrameActive),
      view->GetColorProvider()->GetColor(kColorPipWindowTopBarBackground));
  SaveWidgetScreenshot(view->GetWidget(), "document_pip");
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
// in the sidebar. Behind another app it stays docked while nothing covers it,
// floats once another app's window does, and stays floating when that window
// goes, per T214. Back in front it docks.
IN_PROC_BROWSER_TEST_F(MiniPlayerBrowserTest,
                       MANUAL_BehindAnotherAppItFloatsOnlyOnceCovered) {
  SetAppActiveForTesting(std::nullopt);
  SetCoveredPartForTesting(std::nullopt);
  BringUwToFront();
  ASSERT_TRUE(base::test::RunUntil([] { return IsAppActive(); }));
  MiniPlayer* mini_player = OpenMiniPlayer();
  Play();
  EXPECT_TRUE(
      base::test::RunUntil([&] { return DocksIn(mini_player, browser()); }));
  SaveNativeWindowOverDesktop(browser()->GetWindow()->GetNativeWindow(),
                              "mini_player_docked_real");
  MiniPlayerDock* dock = MiniPlayerDock::From(browser());
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return !dock->GetBoundsInScreen().IsEmpty(); }));
  const gfx::Rect docked = dock->GetBoundsInScreen();

  NSRunningApplication* finder =
      [NSRunningApplication
          runningApplicationsWithBundleIdentifier:@"com.apple.finder"]
          .firstObject;
  ASSERT_TRUE(finder);
  [finder activateWithOptions:0];
  ASSERT_TRUE(base::test::RunUntil([] { return !IsAppActive(); }));
  EXPECT_EQ(0, CoveredPart(dock->GetWidget(), docked));
  Settle(kPastTheHold);
  EXPECT_TRUE(DocksIn(mini_player, browser()));

  base::Process over = CoverWith(docked);
  EXPECT_TRUE(base::test::RunUntil(
      [&] { return CoveredPart(dock->GetWidget(), docked) > 0.9; }));
  EXPECT_TRUE(base::test::RunUntil([&] { return Floats(mini_player); }));
  EXPECT_FALSE(Bool("video.paused"));
  End(over);
  Settle(kPastTheHold);
  EXPECT_TRUE(Floats(mini_player));

  BringUwToFront();
  EXPECT_TRUE(
      base::test::RunUntil([&] { return DocksIn(mini_player, browser()); }));
}

}  // namespace
}  // namespace uw
