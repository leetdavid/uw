// Copyright 2026 The uw Authors. All rights reserved.

#include <algorithm>
#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "base/run_loop.h"
#include "base/task/single_thread_task_runner.h"
#include "base/test/run_until.h"
#include "base/test/scoped_feature_list.h"
#include "base/time/time.h"
#include "chrome/browser/headless/test/headless_mode_browsertest.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface_iterator.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/tabs/common/tab_view.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "ui/base/test/ui_controls.h"
#include "ui/gfx/geometry/point.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/views/view.h"
#include "ui/views/view_utils.h"
#include "ui/views/widget/widget.h"
#include "url/gurl.h"
#include "uw/browser/tab_organization/shared_tree.h"
#include "uw/browser/tab_organization/tab_organization_controller.h"
#include "uw/browser/tab_organization/window_screenshot.h"

namespace uw {
namespace {

constexpr char kMail[] = "data:text/html,<title>Mail</title>";
constexpr char kIssues[] = "data:text/html,<title>Issues</title>";
constexpr char kIssue[] = "data:text/html,<title>Issue 412</title>";

// Drags tabs between windows with a real mouse. A headless window takes no
// drags, so these need a real one. Run them with `pnpm run window-tests`.
class SharedTabsDragBrowserTest : public headless::HeadlessModeBrowserTest {
 public:
  SharedTabsDragBrowserTest() { features_.InitAndEnableFeature(kUwSharedTabs); }

 protected:
  tabs::TabInterface* Open(const char* url) {
    content::WebContents* page =
        chrome::AddAndReturnTabAt(browser(), GURL(url), -1, false);
    EXPECT_TRUE(content::WaitForLoadStop(page));
    base::RunLoop().RunUntilIdle();
    return browser()->tab_strip_model()->GetTabForWebContents(page);
  }

  static tabs::TabInterface* TabAt(Browser* window, const char* url) {
    TabStripModel* tabs = window->tab_strip_model();
    for (int i = 0; i < tabs->count(); ++i) {
      if (tabs->GetWebContentsAt(i)->GetLastCommittedURL() == GURL(url)) {
        return tabs->GetTabAtIndex(i);
      }
    }
    return nullptr;
  }

  static int CountOf(Browser* window, const char* url) {
    TabStripModel* tabs = window->tab_strip_model();
    int count = 0;
    for (int i = 0; i < tabs->count(); ++i) {
      count += tabs->GetWebContentsAt(i)->GetLastCommittedURL() == GURL(url);
    }
    return count;
  }

  static views::View* RowOf(Browser* window, const tabs::TabInterface* tab) {
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

  static void Place(Browser* window, const gfx::Rect& bounds) {
    BrowserView::GetBrowserViewForBrowser(window)->GetWidget()->SetBounds(
        bounds);
    Wait(base::Milliseconds(500));
  }

  // Presses at `from`, moves to `to` in steps, and lets go. A drag that takes
  // a tab out of its window runs a loop of its own until the drop, so each
  // step starts once the one before is done, instead of the test waiting.
  static void Drag(const gfx::Point& from, const gfx::Point& to) {
    ui_controls::EnableUIControls();
    MoveMouse(from);
    PressMouse(ui_controls::DOWN);
    std::vector<gfx::Point> points;
    constexpr int kSteps = 16;
    for (int step = 1; step <= kSteps; ++step) {
      points.emplace_back(from.x() + (to.x() - from.x()) * step / kSteps,
                          from.y() + (to.y() - from.y()) * step / kSteps);
    }
    base::RunLoop dropped;
    Step(std::move(points), 0, dropped.QuitClosure());
    dropped.Run();
    Wait(base::Milliseconds(800));
  }

  static void Step(std::vector<gfx::Point> points,
                   size_t next,
                   base::OnceClosure dropped) {
    if (next == points.size()) {
      ui_controls::SendMouseEventsNotifyWhenDone(
          ui_controls::LEFT, ui_controls::UP, std::move(dropped));
      return;
    }
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
        FROM_HERE,
        base::BindOnce(
            [](std::vector<gfx::Point> points, size_t next,
               base::OnceClosure dropped) {
              const gfx::Point point = points[next];
              ui_controls::SendMouseMoveNotifyWhenDone(
                  point.x(), point.y(),
                  base::BindOnce(&Step, std::move(points), next + 1,
                                 std::move(dropped)));
            },
            std::move(points), next, std::move(dropped)),
        base::Milliseconds(30));
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

  static void Wait(base::TimeDelta delay) {
    base::RunLoop wait;
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
        FROM_HERE, wait.QuitClosure(), delay);
    wait.Run();
  }

 private:
  base::test::ScopedFeatureList features_;
};

// Dragging a tab out to the desktop opens a window on its workspace that
// shows it, and the tab keeps its place in the tree. Dragging it back into
// the first window closes that window, and the tab shows there once.
IN_PROC_BROWSER_TEST_F(SharedTabsDragBrowserTest,
                       MANUAL_ATabDraggedOutAndBackLeavesNoWindow) {
  Browser* first = browser();
  Open(kMail);
  tabs::TabInterface* issues = Open(kIssues);
  Open(kIssue);
  Place(first, gfx::Rect(0, 40, 460, 600));
  TabOrganizationController* controller =
      TabOrganizationController::From(first);
  const PageId id = controller->IdFor(issues).value();
  const int depth_before = controller->DepthFor(issues);
  const int index_before = first->tab_strip_model()->GetIndexOfTab(issues);
  const std::vector<BrowserWindowInterface*> known =
      GetAllBrowserWindowInterfaces();

  Drag(RowOf(first, issues)->GetBoundsInScreen().CenterPoint(),
       gfx::Point(760, 380));
  Browser* own = nullptr;
  ASSERT_TRUE(base::test::RunUntil([&] {
    for (BrowserWindowInterface* window : GetAllBrowserWindowInterfaces()) {
      if (!std::ranges::contains(known, window)) {
        own = window->GetBrowserForMigrationOnly();
      }
    }
    return own && TabAt(own, kMail) && TabAt(own, kIssue) &&
           TabAt(first, kIssues);
  }));
  SaveWindowScreenshot(own, "shared-tabs-dragged-out");
  tabs::TabInterface* out = TabOrganizationController::From(own)->TabFor(id);
  ASSERT_TRUE(out);
  EXPECT_EQ(own->tab_strip_model()->GetActiveTab(), out);
  EXPECT_FALSE(TabOrganizationController::IsCopy(out));
  // The first window's copy is where the tab was.
  tabs::TabInterface* copy = controller->TabFor(id);
  ASSERT_TRUE(copy);
  EXPECT_TRUE(TabOrganizationController::IsCopy(copy));
  EXPECT_EQ(first->tab_strip_model()->GetIndexOfTab(copy), index_before);
  EXPECT_EQ(controller->DepthFor(copy), depth_before);

  // Back into the first window, onto the Mail row.
  Drag(RowOf(own, out)->GetBoundsInScreen().CenterPoint(),
       RowOf(first, TabAt(first, kMail))->GetBoundsInScreen().CenterPoint());
  ASSERT_TRUE(base::test::RunUntil([&] {
    return !std::ranges::contains(GetAllBrowserWindowInterfaces(), own);
  }));
  SaveWindowScreenshot(first, "shared-tabs-dragged-back");
  EXPECT_EQ(CountOf(first, kIssues), 1);
  EXPECT_FALSE(TabOrganizationController::IsCopy(TabAt(first, kIssues)));
}

}  // namespace
}  // namespace uw
