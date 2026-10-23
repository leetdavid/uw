// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#import <AppKit/AppKit.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

#include "base/apple/owned_objc.h"
#include "base/run_loop.h"
#include "base/task/single_thread_task_runner.h"
#include "base/test/run_until.h"
#include "base/time/time.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/headless/test/headless_mode_browsertest.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/profiles/profile_test_util.h"
#include "chrome/browser/themes/theme_service.h"
#include "chrome/browser/themes/theme_service_factory.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/tabs/vertical_tab_strip_state_controller.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/multi_contents_view.h"
#include "chrome/browser/ui/views/frame/tab_strip_region_view.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "ui/base/base_window.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/compositor/layer.h"
#include "ui/events/base_event_utils.h"
#include "ui/events/event.h"
#include "ui/events/event_constants.h"
#import "ui/events/test/cocoa_test_event_utils.h"
#include "ui/gfx/animation/animation.h"
#include "ui/gfx/geometry/point.h"
#include "ui/gfx/geometry/transform.h"
#include "ui/gfx/scoped_animation_duration_scale_mode.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/test/button_test_api.h"
#include "ui/views/widget/widget.h"
#include "ui/views/window/non_client_view.h"
#include "uw/browser/tab_organization/commands.h"
#include "uw/browser/tab_organization/window_screenshot.h"
#include "uw/browser/ui/views/tab_organization/window_commands.h"
#include "uw/browser/ui/views/workspaces/workspace_swipe.h"
#include "uw/browser/ui/views/workspaces/workspace_switcher.h"
#include "uw/browser/workspaces/workspace_list.h"
#include "uw/browser/workspaces/workspace_window.h"

namespace uw {
namespace {

// Far enough to tell the axis, and short of halfway across the sidebar.
constexpr float kShortSwipe = 60;
// Slow enough for no flick: 0.1px per millisecond.
constexpr base::TimeDelta kSlowStep = base::Milliseconds(50);
constexpr float kSlowStepWidth = 5;

class WorkspaceSwipeBrowserTest : public headless::HeadlessModeBrowserTest {
 protected:
  void SetUpOnMainThread() override {
    headless::HeadlessModeBrowserTest::SetUpOnMainThread();
    // The screenshots' size, set before any swipe so it stays put.
    CaptureWindow(browser());
    work_ = WorkspaceWindow::From(browser())->workspace();
  }

  static WorkspaceList& list() { return WorkspaceList::Get(); }

  std::string AddWorkspace(const std::string& name, Profile* profile) {
    const std::string id = list().Add(name, std::string(list().UnusedIcon()),
                                      profile->GetBaseName());
    base::RunLoop().RunUntilIdle();
    return id;
  }

  static Profile& MakeProfile() {
    ProfileManager* manager = g_browser_process->profile_manager();
    return profiles::testing::CreateProfileSync(
        manager, manager->GenerateNextProfileDirectoryPath());
  }

  static WorkspaceSwipe* SwipeOf(BrowserWindowInterface* window) {
    return WorkspaceSwipe::From(window);
  }

  // The window of `member`'s group that shows.
  static BrowserWindowInterface* ShownOf(BrowserWindowInterface* member) {
    std::vector<WorkspaceWindow*> group =
        WorkspaceWindow::From(member)->Partners();
    group.push_back(WorkspaceWindow::From(member));
    for (WorkspaceWindow* window : group) {
      if (!window->hidden()) {
        return window->window();
      }
    }
    return nullptr;
  }

  // Waits until `member`'s group shows `workspace`, and returns its window.
  static BrowserWindowInterface* WaitForShown(BrowserWindowInterface* member,
                                              const std::string& workspace) {
    EXPECT_TRUE(base::test::RunUntil([&] {
      BrowserWindowInterface* shown = ShownOf(member);
      WorkspaceSwipe* swipe = shown ? SwipeOf(shown) : nullptr;
      return shown && WorkspaceWindow::From(shown)->workspace() == workspace &&
             (!swipe || !swipe->has_cover());
    }));
    return ShownOf(member);
  }

  // The middle of `window`'s sidebar, in the window.
  static gfx::Point Sidebar(BrowserWindowInterface* window) {
    views::View* strip = BrowserView::GetBrowserViewForBrowser(window)
                             ->tab_strip_view()
                             ->GetTabStripView();
    gfx::Point point = strip->GetLocalBounds().CenterPoint();
    views::View::ConvertPointToWidget(strip, &point);
    return point;
  }

  // The middle of `window`'s page, in the window.
  static gfx::Point Page(BrowserWindowInterface* window) {
    views::View* page =
        BrowserView::GetBrowserViewForBrowser(window)->multi_contents_view();
    gfx::Point point = page->GetLocalBounds().CenterPoint();
    views::View::ConvertPointToWidget(page, &point);
    return point;
  }

  ui::ScrollEvent MakeScroll(const gfx::Point& point,
                             float x,
                             float y,
                             ui::EventMomentumPhase phase) {
    now_ += kSlowStep;
    return ui::ScrollEvent(ui::EventType::kScroll, gfx::PointF(point),
                           gfx::PointF(point), now_, 0, x, y, x, y, 2, phase);
  }

  // Where the left edge of `window`'s page card is, in the window.
  static int CardLeft(BrowserWindowInterface* window) {
    views::View* page =
        BrowserView::GetBrowserViewForBrowser(window)->multi_contents_view();
    return page->ConvertRectToWidget(page->GetLocalBounds()).x();
  }

  // Checks that `pixels`, a picture of `window`, shows only the tint in the
  // sidebar's strip that ends 11px left of the page card, below the tabs.
  static void ExpectNoEdgeLeftOfTheCard(BrowserWindowInterface* window,
                                        const SkBitmap& pixels) {
    const float scale = static_cast<float>(pixels.width()) /
                        BrowserView::GetBrowserViewForBrowser(window)
                            ->GetWidget()
                            ->GetRootView()
                            ->width();
    const int card = CardLeft(window);
    const int height = static_cast<int>(pixels.height() / scale);
    const auto at = [&](int x, int y) {
      return pixels.getColor(static_cast<int>(x * scale),
                             static_cast<int>(y * scale));
    };
    // Low in the sidebar, under its tabs and over its foot.
    for (int y : {height * 6 / 10, height * 7 / 10, height * 8 / 10}) {
      const SkColor tint = at(card - 30, y);
      for (int x : {card - 13, card - 12, card - 11}) {
        const SkColor color = at(x, y);
        const int difference =
            std::abs(static_cast<int>(SkColorGetR(color)) -
                     static_cast<int>(SkColorGetR(tint))) +
            std::abs(static_cast<int>(SkColorGetG(color)) -
                     static_cast<int>(SkColorGetG(tint))) +
            std::abs(static_cast<int>(SkColorGetB(color)) -
                     static_cast<int>(SkColorGetB(tint)));
        EXPECT_LT(difference, 24) << "at " << x << "," << y << ": "
                                  << std::hex << color << " beside " << tint;
      }
    }
  }

  // Sends a trackpad scroll at `point` in `window`, to whatever is there.
  void Scroll(BrowserWindowInterface* window,
              const gfx::Point& point,
              float x,
              float y,
              ui::EventMomentumPhase phase) {
    ui::ScrollEvent event = MakeScroll(point, x, y, phase);
    BrowserView::GetBrowserViewForBrowser(window)->GetWidget()->OnScrollEvent(
        &event);
  }

  // Hands a trackpad scroll to `window`'s swipe, and says whether it kept
  // the scroll from the tabs.
  bool Kept(BrowserWindowInterface* window,
            float x,
            float y,
            ui::EventMomentumPhase phase) {
    ui::ScrollEvent event = MakeScroll(Sidebar(window), x, y, phase);
    SwipeOf(window)->OnScrollEvent(&event);
    return event.handled();
  }

  // Puts two fingers down on the sidebar and moves them `x` across, in steps
  // `step` apart.
  void MoveFingers(BrowserWindowInterface* window,
                   float x,
                   base::TimeDelta step = kSlowStep) {
    const gfx::Point point = Sidebar(window);
    Scroll(window, point, 0, 0, ui::EventMomentumPhase::MAY_BEGIN);
    const int steps = std::max(1, static_cast<int>(std::abs(x) /
                                                   kSlowStepWidth));
    for (int i = 0; i < steps; ++i) {
      now_ += step - kSlowStep;
      Scroll(window, point, x / steps, 0, ui::EventMomentumPhase::NONE);
    }
  }

  void Lift(BrowserWindowInterface* window) {
    Scroll(window, Sidebar(window), 0, 0, ui::EventMomentumPhase::END);
  }

  // How wide the part of `window`'s sidebar that slides is.
  static float SidebarWidth(BrowserWindowInterface* window) {
    return BrowserView::GetBrowserViewForBrowser(window)
        ->tab_strip_view()
        ->GetTabStripView()
        ->width();
  }

  // A trackpad's NSEvent on `window`'s sidebar, `x` across.
  static NSEvent* TrackpadEvent(BrowserWindowInterface* window,
                                CGFloat x,
                                NSEventPhase phase) {
    NSWindow* ns_window =
        window->GetWindow()->GetNativeWindow().GetNativeNSWindow();
    const gfx::Point sidebar = Sidebar(window);
    return cocoa_test_event_utils::TestScrollEvent(
        NSMakePoint(sidebar.x(),
                    NSHeight(ns_window.contentView.frame) - sidebar.y()),
        ns_window, x, 0, true, phase, NSEventPhaseNone);
  }

  // Hands `window` a trackpad's NSEvent as macOS does.
  static void SendTrackpad(BrowserWindowInterface* window,
                           CGFloat x,
                           NSEventPhase phase) {
    [window->GetWindow()->GetNativeWindow().GetNativeNSWindow().contentView
        scrollWheel:TrackpadEvent(window, x, phase)];
  }

  // The NSEvent's `x` that moves the fingers `fingers` across, rightward
  // positive, whichever way the test's scrolling goes.
  static CGFloat DeltaFor(BrowserWindowInterface* window, float fingers) {
    const ui::ScrollEvent probe(base::apple::OwnedNSEvent(
        TrackpadEvent(window, 10, NSEventPhaseChanged)));
    return FingerDelta(probe) > 0 ? fingers : -fingers;
  }

  // Puts two fingers down on `window`'s sidebar and moves them `fingers`
  // across in 5px NSEvents.
  static void TrackpadMove(BrowserWindowInterface* window, float fingers) {
    SendTrackpad(window, 0, NSEventPhaseMayBegin);
    SendTrackpad(window, 0, NSEventPhaseBegan);
    const CGFloat step = DeltaFor(window, fingers < 0 ? -5 : 5);
    for (int i = 0; i < std::abs(fingers) / 5; ++i) {
      SendTrackpad(window, step, NSEventPhaseChanged);
    }
  }

  // Puts an unloaded copy of `window`'s page in its place, as a restored tab
  // that has not loaded yet.
  static content::WebContents* Unload(BrowserWindowInterface* window) {
    content::WebContents* page =
        window->GetTabStripModel()->GetActiveWebContents();
    content::WebContents::CreateParams params(page->GetBrowserContext());
    params.initially_hidden = true;
    params.desired_renderer_state =
        content::WebContents::CreateParams::kNoRendererProcess;
    std::unique_ptr<content::WebContents> copy =
        content::WebContents::Create(params);
    copy->GetController().CopyStateFrom(&page->GetController(),
                                        /*needs_reload=*/true);
    content::WebContents* unloaded = copy.get();
    window->GetTabStripModel()->DiscardWebContents(page, std::move(copy));
    return unloaded;
  }

  // Swipes `window` all the way to its next workspace, and waits for it.
  BrowserWindowInterface* SwipeToNext(BrowserWindowInterface* window,
                                      const std::string& next) {
    MoveFingers(window, -SidebarWidth(window) * 0.75f);
    Lift(window);
    return WaitForShown(window, next);
  }

  std::string work_;
  base::TimeTicks now_ = base::TimeTicks::Now();
};

IN_PROC_BROWSER_TEST_F(WorkspaceSwipeBrowserTest,
                       TheSidebarFollowsTheFingersAndThePageStays) {
  const std::string reading = AddWorkspace("Reading", browser()->GetProfile());
  MoveFingers(browser(), -kShortSwipe);
  WorkspaceSwipe* swipe = SwipeOf(browser());
  ASSERT_TRUE(base::test::RunUntil([&] { return swipe->shows_slide(); }));
  EXPECT_NEAR(swipe->offset(), -kShortSwipe, 0.5f);
  EXPECT_EQ(swipe->target(), reading);
  // Only the sidebar moves until the swipe lands.
  ui::Layer* page = BrowserView::GetBrowserViewForBrowser(browser())
                        ->multi_contents_view()
                        ->layer();
  EXPECT_TRUE(!page || page->transform().IsIdentity());
  EXPECT_EQ(ShownOf(browser()), browser());
  SaveWindowScreenshot(browser(), "swipe-started");
  Lift(browser());
  ASSERT_TRUE(base::test::RunUntil([&] { return !swipe->sliding(); }));
}

IN_PROC_BROWSER_TEST_F(WorkspaceSwipeBrowserTest, AShortSwipeSlidesBack) {
  AddWorkspace("Reading", browser()->GetProfile());
  MoveFingers(browser(), -kShortSwipe);
  Lift(browser());
  WorkspaceSwipe* swipe = SwipeOf(browser());
  ASSERT_TRUE(base::test::RunUntil([&] { return !swipe->sliding(); }));
  EXPECT_EQ(swipe->offset(), 0);
  EXPECT_FALSE(swipe->shows_slide());
  EXPECT_EQ(ShownOf(browser()), browser());
  // Nothing opened the other workspace's window.
  EXPECT_TRUE(WorkspaceWindow::From(browser())->Partners().empty());
}

IN_PROC_BROWSER_TEST_F(WorkspaceSwipeBrowserTest,
                       PastHalfwayTheOtherWorkspaceLands) {
  const std::string reading = AddWorkspace("Reading", browser()->GetProfile());
  BrowserWindowInterface* shown = SwipeToNext(browser(), reading);
  ASSERT_NE(shown, browser());
  EXPECT_TRUE(WorkspaceWindow::From(browser())->hidden());
  EXPECT_FALSE(SwipeOf(browser())->sliding());
  // The window left keeps a picture of its sidebar for the next swipe.
  EXPECT_FALSE(SwipeOf(browser())->picture().isNull());
  ui::Layer* page =
      BrowserView::GetBrowserViewForBrowser(shown)
          ->multi_contents_view()
          ->layer();
  EXPECT_TRUE(!page || page->transform().IsIdentity());
}

IN_PROC_BROWSER_TEST_F(WorkspaceSwipeBrowserTest,
                       PastHalfwayTheOtherPageLoadsBeforeTheFingersLift) {
  ASSERT_TRUE(embedded_test_server()->Start());
  const GURL article = embedded_test_server()->GetURL("/title1.html");
  const std::string reading = AddWorkspace("Reading", browser()->GetProfile());
  WorkspaceWindow::From(browser())->Switch(reading);
  BrowserWindowInterface* partner = WaitForShown(browser(), reading);
  ASSERT_TRUE(content::NavigateToURL(
      partner->GetTabStripModel()->GetActiveWebContents(), article));
  WorkspaceWindow::From(partner)->Switch(work_);
  WaitForShown(partner, work_);
  content::WebContents* page = Unload(partner);

  const float half = SidebarWidth(browser()) / 2;
  MoveFingers(browser(), -(half - 10));
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(page->GetController().NeedsReload());
  for (int i = 0; i < 4; ++i) {
    Scroll(browser(), Sidebar(browser()), -kSlowStepWidth, 0,
           ui::EventMomentumPhase::NONE);
  }
  EXPECT_FALSE(page->GetController().NeedsReload());
  EXPECT_TRUE(base::test::RunUntil([&] {
    return page->GetPrimaryMainFrame()->GetLastCommittedURL() == article;
  }));
  EXPECT_TRUE(SwipeOf(browser())->sliding());
  EXPECT_EQ(ShownOf(browser()), browser());
  Lift(browser());
  EXPECT_EQ(WaitForShown(browser(), reading), partner);
}

IN_PROC_BROWSER_TEST_F(WorkspaceSwipeBrowserTest, AFlickLands) {
  const std::string reading = AddWorkspace("Reading", browser()->GetProfile());
  // 40px at 1px per millisecond.
  MoveFingers(browser(), -40, base::Milliseconds(5));
  Lift(browser());
  WaitForShown(browser(), reading);
}

IN_PROC_BROWSER_TEST_F(WorkspaceSwipeBrowserTest,
                       PastTheFirstWorkspaceTheSidebarOnlyStretches) {
  AddWorkspace("Reading", browser()->GetProfile());
  ASSERT_EQ(list().IndexOf(work_), 0);
  MoveFingers(browser(), SidebarWidth(browser()));
  WorkspaceSwipe* swipe = SwipeOf(browser());
  EXPECT_TRUE(swipe->target().empty());
  EXPECT_GT(swipe->offset(), 0);
  EXPECT_LT(swipe->offset(), 40);
  Lift(browser());
  ASSERT_TRUE(base::test::RunUntil([&] { return !swipe->sliding(); }));
  EXPECT_EQ(ShownOf(browser()), browser());
}

IN_PROC_BROWSER_TEST_F(WorkspaceSwipeBrowserTest,
                       ScrollsUpAndDownAndTheMomentumOfASwipe) {
  AddWorkspace("Reading", browser()->GetProfile());
  using enum ui::EventMomentumPhase;
  EXPECT_FALSE(Kept(browser(), 0, 0, MAY_BEGIN));
  EXPECT_FALSE(Kept(browser(), 2, -30, NONE));
  EXPECT_FALSE(Kept(browser(), 20, -10, NONE));
  EXPECT_FALSE(SwipeOf(browser())->sliding());
  EXPECT_FALSE(Kept(browser(), 0, 0, END));
  EXPECT_FALSE(Kept(browser(), 0, -20, INERTIAL_UPDATE));

  EXPECT_FALSE(Kept(browser(), 0, 0, MAY_BEGIN));
  EXPECT_FALSE(Kept(browser(), -10, 0, NONE));
  EXPECT_TRUE(Kept(browser(), -10, 0, NONE));
  EXPECT_TRUE(SwipeOf(browser())->sliding());
  EXPECT_TRUE(Kept(browser(), 0, 0, END));
  EXPECT_TRUE(Kept(browser(), -20, 0, INERTIAL_UPDATE));
  EXPECT_TRUE(Kept(browser(), 0, -20, INERTIAL_UPDATE));
  EXPECT_TRUE(Kept(browser(), 0, 0, END));
  EXPECT_FALSE(Kept(browser(), 0, -20, INERTIAL_UPDATE));
}

IN_PROC_BROWSER_TEST_F(WorkspaceSwipeBrowserTest, ASwipeOnThePageIsNotASlide) {
  AddWorkspace("Reading", browser()->GetProfile());
  const gfx::Point point = Page(browser());
  Scroll(browser(), point, 0, 0, ui::EventMomentumPhase::MAY_BEGIN);
  for (int i = 0; i < 10; ++i) {
    Scroll(browser(), point, -10, 0, ui::EventMomentumPhase::NONE);
  }
  EXPECT_FALSE(SwipeOf(browser())->sliding());
}

IN_PROC_BROWSER_TEST_F(WorkspaceSwipeBrowserTest,
                       TheNarrowSidebarDoesNotSwipe) {
  AddWorkspace("Reading", browser()->GetProfile());
  auto* state = tabs::VerticalTabStripStateController::From(browser());
  state->RequestCollapse(true);
  ASSERT_TRUE(base::test::RunUntil([&] { return state->IsCollapsed(); }));
  MoveFingers(browser(), -kShortSwipe);
  EXPECT_FALSE(SwipeOf(browser())->sliding());
}

IN_PROC_BROWSER_TEST_F(WorkspaceSwipeBrowserTest,
                       KeysAndClicksPlayTheSlideAndThePageFades) {
  const std::string reading = AddWorkspace("Reading", browser()->GetProfile());
  RunWindowCommand(browser(), kGoToWorkspace1 + 1);
  WorkspaceSwipe* swipe = SwipeOf(browser());
  EXPECT_TRUE(swipe->sliding());
  ASSERT_TRUE(base::test::RunUntil([&] { return swipe->shows_slide(); }));
  BrowserWindowInterface* shown = WaitForShown(browser(), reading);

  views::Button* work = WorkspaceSwitcher::From(shown)->IconFor(work_);
  ASSERT_TRUE(work);
  views::test::ButtonTestApi(work).NotifyClick(
      ui::MouseEvent(ui::EventType::kMousePressed, gfx::Point(), gfx::Point(),
                     ui::EventTimeForNow(), ui::EF_LEFT_MOUSE_BUTTON, 0));
  EXPECT_TRUE(SwipeOf(shown)->sliding());
  WaitForShown(browser(), work_);
  // Both windows have slid away once, so each has a picture for the next.
  EXPECT_FALSE(SwipeOf(shown)->picture().isNull());
}

IN_PROC_BROWSER_TEST_F(WorkspaceSwipeBrowserTest,
                       WithReduceMotionAKeyOnlyFadesThePage) {
  gfx::Animation::SetPrefersReducedMotionForTesting(true);
  const std::string reading = AddWorkspace("Reading", browser()->GetProfile());
  RunWindowCommand(browser(), kNextWorkspace);
  WorkspaceSwipe* swipe = SwipeOf(browser());
  EXPECT_TRUE(swipe->sliding());
  WaitForShown(browser(), reading);
  EXPECT_FALSE(swipe->shows_slide());
  EXPECT_TRUE(swipe->picture().isNull());
  gfx::Animation::SetPrefersReducedMotionForTesting(false);
}

IN_PROC_BROWSER_TEST_F(WorkspaceSwipeBrowserTest,
                       TheSecondSwipeShowsThePictureOfTheWindowLeft) {
  const std::string reading = AddWorkspace("Reading", browser()->GetProfile());
  BrowserWindowInterface* shown = SwipeToNext(browser(), reading);
  ASSERT_FALSE(SwipeOf(browser())->picture().isNull());
  MoveFingers(shown, SidebarWidth(shown) * 0.5f);
  WorkspaceSwipe* swipe = SwipeOf(shown);
  ASSERT_TRUE(base::test::RunUntil([&] { return swipe->shows_slide(); }));
  EXPECT_EQ(swipe->target(), work_);
  SaveWindowScreenshot(shown->GetBrowserForMigrationOnly(),
                       "swipe-halfway-back");
  Lift(shown);
  ASSERT_TRUE(base::test::RunUntil([&] { return !swipe->sliding(); }));
}

IN_PROC_BROWSER_TEST_F(WorkspaceSwipeBrowserTest,
                       TheSlideInDarkModeAndTowardAnotherProfile) {
  ThemeServiceFactory::GetForProfile(browser()->GetProfile())
      ->SetBrowserColorScheme(ThemeService::BrowserColorScheme::kDark);
  Profile& personal = MakeProfile();
  const std::string home = AddWorkspace("Home", &personal);
  // Home's window opens, then slides away, so the next slide has its picture
  // and its tint.
  BrowserWindowInterface* shown = SwipeToNext(browser(), home);
  RunWindowCommand(shown, kPreviousWorkspace);
  BrowserWindowInterface* work = WaitForShown(browser(), work_);
  ASSERT_FALSE(SwipeOf(shown)->picture().isNull());
  MoveFingers(work, -SidebarWidth(work) * 0.5f);
  WorkspaceSwipe* swipe = SwipeOf(work);
  ASSERT_TRUE(base::test::RunUntil([&] { return swipe->shows_slide(); }));
  EXPECT_EQ(swipe->target(), home);
  SaveWindowScreenshot(work->GetBrowserForMigrationOnly(),
                       "swipe-halfway-other-profile-dark");
  Lift(work);
  ASSERT_TRUE(base::test::RunUntil([&] { return !swipe->sliding(); }));
}


// The phases AppKit gives a real trackpad's NSEvents.
IN_PROC_BROWSER_TEST_F(WorkspaceSwipeBrowserTest,
                       ATrackpadSwipeThroughAppKitLands) {
  const std::string reading = AddWorkspace("Reading", browser()->GetProfile());
  TrackpadMove(browser(), -SidebarWidth(browser()) * 0.75f);
  EXPECT_TRUE(SwipeOf(browser())->sliding());
  EXPECT_EQ(SwipeOf(browser())->target(), reading);
  SendTrackpad(browser(), 0, NSEventPhaseEnded);
  WaitForShown(browser(), reading);
}

// In the VM, through pnpm run window-tests: the slide in a real window, with
// pictures on both sides.
IN_PROC_BROWSER_TEST_F(WorkspaceSwipeBrowserTest, MANUAL_ASwipeInARealWindow) {
  const std::string reading = AddWorkspace("Reading", browser()->GetProfile());
  RunWindowCommand(browser(), kNextWorkspace);
  BrowserWindowInterface* shown = WaitForShown(browser(), reading);
  RunWindowCommand(shown, kPreviousWorkspace);
  WaitForShown(browser(), work_);
  ASSERT_FALSE(SwipeOf(shown)->picture().isNull());

  const float width = SidebarWidth(browser());
  WorkspaceSwipe* swipe = SwipeOf(browser());
  TrackpadMove(browser(), -width * 0.25f);
  ASSERT_TRUE(base::test::RunUntil([&] { return swipe->shows_slide(); }));
  SaveNativeWindowScreenshot(browser(), "swipe-real-a-quarter");
  for (int i = 0; i < width * 0.25f / 5; ++i) {
    SendTrackpad(browser(), DeltaFor(browser(), -5), NSEventPhaseChanged);
  }
  EXPECT_NEAR(swipe->offset(), -width * 0.5f, 5);
  SaveNativeWindowScreenshot(browser(), "swipe-real-halfway");
  for (int i = 0; i < width * 0.25f / 5; ++i) {
    SendTrackpad(browser(), DeltaFor(browser(), -5), NSEventPhaseChanged);
  }
  SaveNativeWindowScreenshot(browser(), "swipe-real-three-quarters");
  SendTrackpad(browser(), 0, NSEventPhaseEnded);
  shown = WaitForShown(browser(), reading);
  SaveNativeWindowScreenshot(shown->GetBrowserForMigrationOnly(),
                             "swipe-real-landed");

  // Back the other way, whose page comes in from the left: nothing of the
  // card's edge stays over the sidebar.
  // The card has an edge in dark mode.
  ThemeServiceFactory::GetForProfile(browser()->GetProfile())
      ->SetBrowserColorScheme(ThemeService::BrowserColorScheme::kDark);
  RunWindowCommand(shown, kPreviousWorkspace);
  WaitForShown(browser(), work_);
  base::RunLoop settle;
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE, settle.QuitClosure(), base::Milliseconds(500));
  settle.Run();
  ExpectNoEdgeLeftOfTheCard(browser(), CaptureNativeWindow(browser()));
  SaveNativeWindowScreenshot(browser(), "swipe-real-back");
}


// Whatever draws the card's edge while the page is still nudged draws it where
// the card is laid out, so no outline stays over the sidebar after the fade.
IN_PROC_BROWSER_TEST_F(WorkspaceSwipeBrowserTest,
                       NoEdgeIsLeftOverTheSidebarAfterThePageFades) {
  // The card has an edge in dark mode.
  ThemeServiceFactory::GetForProfile(browser()->GetProfile())
      ->SetBrowserColorScheme(ThemeService::BrowserColorScheme::kDark);
  gfx::ScopedAnimationDurationScaleMode slow(10);
  const std::string reading = AddWorkspace("Reading", browser()->GetProfile());
  RunWindowCommand(browser(), kNextWorkspace);
  BrowserWindowInterface* shown = WaitForShown(browser(), reading);
  // Back to the workspace before, whose page comes in from 12px to the left.
  RunWindowCommand(shown, kPreviousWorkspace);
  ASSERT_TRUE(base::test::RunUntil([&] {
    return ShownOf(browser()) == browser() && SwipeOf(browser())->has_cover();
  }));
  BrowserView* view = BrowserView::GetBrowserViewForBrowser(browser());
  view->GetWidget()->non_client_view()->frame_view()->SchedulePaint();
  view->tab_strip_view()->SchedulePaint();
  base::RunLoop loop;
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE, loop.QuitClosure(), base::Milliseconds(200));
  loop.Run();
  ASSERT_TRUE(SwipeOf(browser())->has_cover());
  WaitForShown(browser(), work_);

  ExpectNoEdgeLeftOfTheCard(browser(), CaptureWindow(browser()));
  SaveWindowScreenshot(browser(), "swipe-edge-after-fade");
}

}  // namespace
}  // namespace uw
