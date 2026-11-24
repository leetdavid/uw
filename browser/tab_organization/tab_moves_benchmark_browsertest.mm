// Copyright 2026 The uw Authors. All rights reserved.

#import <AppKit/AppKit.h>
#include <CoreFoundation/CoreFoundation.h>

#include <algorithm>
#include <string>
#include <vector>

#include "base/functional/bind.h"
#include "base/run_loop.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/stringprintf.h"
#include "base/strings/sys_string_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/single_thread_task_runner.h"
#include "base/test/run_until.h"
#include "base/time/time.h"
#include "chrome/browser/headless/test/headless_mode_browsertest.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/navigator/browser_navigator.h"
#include "chrome/browser/ui/navigator/browser_navigator_params.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/tabs/common/tab_view.h"
#include "chrome/browser/ui/views/tabs/dragging/tab_drag_controller.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/render_process_host.h"
#include "content/public/test/browser_test.h"
#include "ui/base/test/ui_controls.h"
#include "ui/events/keycodes/keyboard_codes.h"
#import "ui/events/test/cocoa_test_event_utils.h"
#include "ui/gfx/geometry/point.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/mac/coordinate_conversion.h"
#include "ui/views/controls/scroll_view.h"
#include "ui/views/view.h"
#include "ui/views/view_utils.h"
#include "ui/views/widget/widget.h"
#include "url/gurl.h"
#include "uw/browser/tab_organization/tab_organization_controller.h"
#include "uw/browser/workspaces/workspace_list.h"
#include "uw/browser/workspaces/workspace_window.h"

namespace uw {
namespace {

// One frame at 120 Hz, the tab move budget in docs/architecture.md.
constexpr base::TimeDelta kBudget = base::Microseconds(8333);
// The time between steps: long enough for a step's work to finish before
// the next starts, so each step's work is measured on its own.
constexpr base::TimeDelta kStepGap = base::Milliseconds(40);

// How long the main thread works, as wall time less the time its run loop
// waits for something to do. Waiting on another process, such as the window
// server, counts as work, since nothing else runs meanwhile.
class MainThreadWork {
 public:
  MainThreadWork() {
    observer_ = CFRunLoopObserverCreateWithHandler(
        kCFAllocatorDefault, kCFRunLoopBeforeWaiting | kCFRunLoopAfterWaiting,
        /*repeats=*/true, /*order=*/0,
        ^(CFRunLoopObserverRef, CFRunLoopActivity activity) {
          const base::TimeTicks now = base::TimeTicks::Now();
          if (activity == kCFRunLoopBeforeWaiting) {
            asleep_since_ = now;
          } else if (!asleep_since_.is_null()) {
            idle_ += now - asleep_since_;
            asleep_since_ = base::TimeTicks();
          }
        });
    CFRunLoopAddObserver(CFRunLoopGetMain(), observer_, kCFRunLoopCommonModes);
    Lap();
  }

  ~MainThreadWork() {
    CFRunLoopRemoveObserver(CFRunLoopGetMain(), observer_,
                            kCFRunLoopCommonModes);
    CFRelease(observer_);
  }

  // The work since the last lap.
  base::TimeDelta Lap() {
    const base::TimeTicks now = base::TimeTicks::Now();
    const base::TimeDelta work = (now - lap_start_) - idle_;
    lap_start_ = now;
    idle_ = base::TimeDelta();
    return work;
  }

 private:
  CFRunLoopObserverRef observer_;
  base::TimeTicks lap_start_;
  base::TimeTicks asleep_since_;
  base::TimeDelta idle_;
};

std::string Summary(const char* name, std::vector<base::TimeDelta> steps) {
  std::ranges::sort(steps);
  const auto at = [&](double share) {
    return steps[std::min(steps.size() - 1,
                          static_cast<size_t>(share * steps.size()))]
        .InMillisecondsF();
  };
  const auto over = std::ranges::count_if(
      steps, [](base::TimeDelta step) { return step > kBudget; });
  return base::StringPrintf(
      "%s: %zu steps, median %.2f ms, p90 %.2f ms, p95 %.2f ms, max %.2f "
      "ms, %td over %.1f ms",
      name, steps.size(), at(0.5), at(0.9), at(0.95),
      steps.back().InMillisecondsF(), over, kBudget.InMillisecondsF());
}

// Drags tabs and holds a move key in a window set up like the owner's on
// 2026-11-24, and measures the main thread's work for each step. Needs a real
// window: run it with `pnpm run window-tests`.
class TabMovesBenchmarkBrowserTest : public headless::HeadlessModeBrowserTest {
 protected:
  void SetUpOnMainThread() override {
    headless::HeadlessModeBrowserTest::SetUpOnMainThread();
    // Dozens of pages, each in a process of its own, are more than the VM
    // holds. The pages share a few, which changes nothing measured here.
    content::RenderProcessHost::SetMaxRendererProcessCount(3);
  }

  Profile* profile() { return browser()->GetProfile(); }

  static void Wait(base::TimeDelta delay) {
    base::RunLoop wait;
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
        FROM_HERE, wait.QuitClosure(), delay);
    wait.Run();
  }

  static tabs::TabInterface* Open(BrowserWindowInterface* window,
                                  const std::string& name,
                                  tabs::TabInterface* parent = nullptr) {
    NavigateParams params(
        window->GetBrowserForMigrationOnly(),
        GURL("data:text/html,<title>" + name + "</title>"),
        parent ? ui::PAGE_TRANSITION_LINK : ui::PAGE_TRANSITION_TYPED);
    params.disposition = WindowOpenDisposition::NEW_BACKGROUND_TAB;
    if (parent) {
      params.source_contents = parent->GetContents();
    }
    Navigate(&params);
    return window->GetTabStripModel()->GetTabForWebContents(
        params.navigated_or_inserted_contents);
  }

  // `count` tabs in `window`: runs of four, each a page with a child, a
  // grandchild, and a great-grandchild, so the tree goes three levels deep.
  static std::vector<tabs::TabInterface*> OpenNested(
      BrowserWindowInterface* window,
      const std::string& prefix,
      int count) {
    std::vector<tabs::TabInterface*> opened;
    tabs::TabInterface* parent = nullptr;
    for (int i = 0; i < count; ++i) {
      parent = Open(window, prefix + base::NumberToString(i),
                    i % 4 == 0 ? nullptr : parent);
      opened.push_back(parent);
    }
    base::RunLoop().RunUntilIdle();
    return opened;
  }

  static void MakeTask(BrowserWindowInterface* window,
                       const std::vector<tabs::TabInterface*>& tabs,
                       const std::string& title) {
    TabOrganizationController* controller =
        TabOrganizationController::From(window);
    std::vector<PageId> ids;
    for (const tabs::TabInterface* tab : tabs) {
      ids.push_back(controller->IdFor(tab).value());
    }
    ASSERT_TRUE(controller->MoveToNewTask(ids, base::UTF8ToUTF16(title)));
    base::RunLoop().RunUntilIdle();
  }

  static BrowserWindowInterface* PartnerFor(BrowserWindowInterface* window,
                                            const std::string& workspace) {
    BrowserWindowInterface* partner = nullptr;
    WorkspaceWindow::From(window)->WithPartner(
        workspace,
        base::BindOnce([](BrowserWindowInterface** out,
                          BrowserWindowInterface* found) { *out = found; },
                       &partner));
    EXPECT_TRUE(base::test::RunUntil([&] { return partner; }));
    return partner;
  }

  static views::View* RowOf(BrowserWindowInterface* window,
                            const tabs::TabInterface* tab) {
    return Find(BrowserView::GetBrowserViewForBrowser(window), tab);
  }

  static views::View* Find(views::View* view, const tabs::TabInterface* tab) {
    if (auto* row = views::AsViewClass<TabView>(view);
        row && row->GetTabInterface() == tab) {
      return row;
    }
    for (views::View* child : view->children()) {
      if (views::View* row = Find(child, tab)) {
        return row;
      }
    }
    return nullptr;
  }

  static void MoveMouse(const gfx::Point& point) {
    base::RunLoop moved;
    ASSERT_TRUE(ui_controls::SendMouseMoveNotifyWhenDone(point.x(), point.y(),
                                                         moved.QuitClosure()));
    moved.Run();
  }

  static void PressMouse(int state) {
    base::RunLoop done;
    ASSERT_TRUE(ui_controls::SendMouseEventsNotifyWhenDone(
        ui_controls::LEFT, state, done.QuitClosure()));
    done.Run();
  }

  // Three workspaces in two groups of windows, as the owner's: the first
  // with 48 tabs, 32 of them in four tasks, three levels deep; the second
  // with 26 tabs and a task; the third with a few. Only the first group's
  // window on the first workspace shows. Returns the tabs of the first task.
  std::vector<tabs::TabInterface*> SetUpLikeTheOwners() {
    const std::string reading = WorkspaceList::Get().Add(
        "Reading", std::string(WorkspaceList::Get().UnusedIcon()),
        profile()->GetBaseName());
    const std::string errands = WorkspaceList::Get().Add(
        "Errands", std::string(WorkspaceList::Get().UnusedIcon()),
        profile()->GetBaseName());
    base::RunLoop().RunUntilIdle();

    std::vector<tabs::TabInterface*> first_task;
    for (int task = 0; task < 4; ++task) {
      std::vector<tabs::TabInterface*> tabs =
          OpenNested(browser(), "task" + base::NumberToString(task) + "-", 8);
      MakeTask(browser(), tabs, "Task " + base::NumberToString(task));
      if (task == 0) {
        first_task = tabs;
      }
    }
    OpenNested(browser(), "scratch-", 16);

    BrowserWindowInterface* reading_window = PartnerFor(browser(), reading);
    std::vector<tabs::TabInterface*> reading_task =
        OpenNested(reading_window, "reading-task-", 2);
    MakeTask(reading_window, reading_task, "Reading");
    OpenNested(reading_window, "reading-", 24);
    BrowserWindowInterface* errands_window = PartnerFor(browser(), errands);
    OpenNested(errands_window, "errand-", 3);

    // The second group: another window on each workspace, which copies the
    // tabs, and which shows the third workspace behind the first window.
    Browser* second = CreateBrowser(profile());
    PartnerFor(second, reading);
    BrowserWindowInterface* second_errands = PartnerFor(second, errands);
    EXPECT_TRUE(WorkspaceWindow::From(second)->Switch(errands));
    EXPECT_TRUE(base::test::RunUntil([&] {
      return second_errands->GetWindow()->IsVisible() &&
             !second->GetWindow()->IsVisible();
    }));
    // Only the first window shows, as on the owner's screen. A minimized
    // window still counts when macOS is asked which window is where.
    [second_errands->GetWindow()->GetNativeWindow().GetNativeNSWindow()
        miniaturize:nil];

    BrowserView::GetBrowserViewForBrowser(browser())->GetWidget()->SetBounds(
        gfx::Rect(0, 40, 980, 720));
    browser()->GetWindow()->Activate();
    Wait(base::Seconds(2));
    return first_task;
  }
};

IN_PROC_BROWSER_TEST_F(TabMovesBenchmarkBrowserTest,
                       MANUAL_EachDragStepAndKeyMoveKeepsToTheBudget) {
  const std::vector<tabs::TabInterface*> task = SetUpLikeTheOwners();
  ui_controls::EnableUIControls();

  // A drag: down the sidebar past ten rows and back, in 6 px steps, as a
  // trackpad moves.
  std::vector<base::TimeDelta> drag;
  {
    // The sidebar shows its top, where the first task is, as it does before
    // the user scrolls. It had scrolled to the last tab opened.
    for (views::View* view = RowOf(browser(), task[1]); view;
         view = view->parent()) {
      if (auto* scroll = views::AsViewClass<views::ScrollView>(view)) {
        scroll->ScrollToOffset(gfx::PointF());
        break;
      }
    }
    Wait(base::Milliseconds(500));
    const gfx::Point from =
        RowOf(browser(), task[1])->GetBoundsInScreen().CenterPoint();
    TabStripModel* tabs = browser()->tab_strip_model();
    const int start = tabs->GetIndexOfTab(task[1]);
    // The press lands on the first window, not on another in front of it.
    NSWindow* front =
        browser()->GetWindow()->GetNativeWindow().GetNativeNSWindow();
    const NSPoint point = gfx::ScreenPointToNSPoint(from);
    ASSERT_EQ([NSWindow windowNumberAtPoint:point
                  belowWindowWithWindowNumber:0],
              front.windowNumber)
        << "visible " << browser()->GetWindow()->IsVisible() << " ns visible "
        << front.visible << " on space " << front.onActiveSpace << " frame "
        << base::SysNSStringToUTF8(NSStringFromRect(front.frame)) << " point "
        << base::SysNSStringToUTF8(NSStringFromPoint(point)) << " from "
        << from.ToString() << " alpha " << front.alphaValue;
    MoveMouse(from);
    PressMouse(ui_controls::DOWN);
    MainThreadWork work;
    const int row = RowOf(browser(), task[1])->height();
    const int steps = row * 10 / 6;
    for (int step = 1; step <= 2 * steps; ++step) {
      const int dy = 6 * (step <= steps ? step : 2 * steps - step);
      work.Lap();
      MoveMouse(gfx::Point(from.x(), from.y() + dy));
      Wait(kStepGap);
      drag.push_back(work.Lap());
      // The steps measured a drag: one ran, and the tab moved with it.
      if (step == steps) {
        EXPECT_TRUE(TabDragController::IsActive());
        EXPECT_NE(tabs->GetIndexOfTab(task[1]), start);
      }
    }
    PressMouse(ui_controls::UP);
    Wait(base::Seconds(1));
  }

  // A held move key: Ctrl+Cmd+Down ten times, then Up ten times, as key
  // repeat sends them.
  std::vector<base::TimeDelta> keys;
  {
    browser()->tab_strip_model()->ActivateTabAt(
        browser()->tab_strip_model()->GetIndexOfTab(task[0]));
    Wait(base::Milliseconds(500));
    NSWindow* window =
        browser()->GetWindow()->GetNativeWindow().GetNativeNSWindow();
    TabStripModel* tabs = browser()->tab_strip_model();
    const int start = tabs->GetIndexOfTab(task[0]);
    MainThreadWork work;
    for (int press = 0; press < 20; ++press) {
      const ui::KeyboardCode key = press < 10 ? ui::VKEY_DOWN : ui::VKEY_UP;
      work.Lap();
      for (const bool down : {true, false}) {
        [NSApp postEvent:cocoa_test_event_utils::SynthesizeKeyEvent(
                             window, down, key,
                             NSEventModifierFlagControl |
                                 NSEventModifierFlagCommand)
                 atStart:NO];
      }
      Wait(kStepGap);
      keys.push_back(work.Lap());
      // The presses measured moves: the tab went down.
      if (press == 9) {
        EXPECT_NE(tabs->GetIndexOfTab(task[0]), start);
      }
    }
  }

  LOG(WARNING) << Summary("drag", drag);
  LOG(WARNING) << Summary("keys", keys);
  const auto p95 = [](std::vector<base::TimeDelta> steps) {
    std::ranges::sort(steps);
    return steps[steps.size() * 95 / 100];
  };
  EXPECT_LE(p95(drag), kBudget) << Summary("drag", drag);
  EXPECT_LE(p95(keys), kBudget) << Summary("keys", keys);
}

}  // namespace
}  // namespace uw
