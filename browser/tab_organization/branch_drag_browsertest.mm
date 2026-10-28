// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include <optional>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "base/run_loop.h"
#include "base/task/single_thread_task_runner.h"
#include "base/time/time.h"
#include "chrome/browser/headless/test/headless_mode_browsertest.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/navigator/browser_navigator.h"
#include "chrome/browser/ui/navigator/browser_navigator_params.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/tabs/common/tab_view.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/test/browser_test.h"
#include "ui/base/test/ui_controls.h"
#include "ui/gfx/geometry/point.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/views/view.h"
#include "ui/views/view_utils.h"
#include "ui/views/widget/widget.h"
#include "url/gurl.h"
#include "uw/browser/tab_organization/tab_organization_controller.h"
#include "uw/browser/tab_organization/window_screenshot.h"

namespace uw {
namespace {

// Drags tabs in the sidebar with a real mouse. A headless window takes no
// drags, so these need a real one. Run them with `pnpm run window-tests`.
class BranchDragBrowserTest : public headless::HeadlessModeBrowserTest {
 protected:
  TabOrganizationController* controller() {
    return TabOrganizationController::From(browser());
  }

  TabStripModel* tabs() { return browser()->tab_strip_model(); }

  // Opens a background tab the way Cmd-clicking a link in `source` does.
  tabs::TabInterface* OpenLinkFrom(tabs::TabInterface* source) {
    NavigateParams params(browser(), GURL("about:blank"),
                          ui::PAGE_TRANSITION_LINK);
    params.disposition = WindowOpenDisposition::NEW_BACKGROUND_TAB;
    params.source_contents = source->GetContents();
    Navigate(&params);
    base::RunLoop().RunUntilIdle();
    return tabs()->GetTabForWebContents(params.navigated_or_inserted_contents);
  }

  // A parent with two children, then another tab, in the Scratchpad.
  void StageBranch() {
    parent_ = tabs()->GetTabAtIndex(0);
    first_ = OpenLinkFrom(parent_);
    second_ = OpenLinkFrom(parent_);
    chrome::NewTab(browser(), NewTabTypes::kNewTabCommand);
    other_ = tabs()->GetActiveTab();
    tabs()->ActivateTabAt(0);
    Wait(base::Milliseconds(500));
    ASSERT_EQ(Order(), (std::vector<tabs::TabInterface*>{parent_, first_,
                                                         second_, other_}));
    ASSERT_EQ(controller()->DepthFor(first_), 1);
  }

  std::vector<tabs::TabInterface*> Order() {
    std::vector<tabs::TabInterface*> order;
    for (int i = 0; i < tabs()->count(); ++i) {
      order.push_back(tabs()->GetTabAtIndex(i));
    }
    return order;
  }

  views::View* RowOf(const tabs::TabInterface* tab) {
    return Find(BrowserView::GetBrowserViewForBrowser(browser()), tab);
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

  // Drags `tab`'s row to just below `onto`'s, holding Option when `alone`.
  void DragBelow(const tabs::TabInterface* tab,
                 const tabs::TabInterface* onto,
                 bool alone) {
    const gfx::Rect target = RowOf(onto)->GetBoundsInScreen();
    Drag(tab, gfx::Point(target.CenterPoint().x(), target.bottom() - 3), alone);
  }

  // Drags `tab`'s row `dx` sideways, where it is.
  void DragSideways(const tabs::TabInterface* tab,
                    int dx,
                    const char* shot = nullptr) {
    const gfx::Point from = RowOf(tab)->GetBoundsInScreen().CenterPoint();
    Drag(tab, gfx::Point(from.x() + dx, from.y()), /*alone=*/false, shot);
  }

  // Drags `tab`'s row to `to`, holding Option when `alone`. Saves a picture
  // of the window named `shot` just before the drop.
  void Drag(const tabs::TabInterface* tab,
            const gfx::Point& to,
            bool alone,
            const char* shot = nullptr) {
    ui_controls::EnableUIControls();
    const gfx::Point from = RowOf(tab)->GetBoundsInScreen().CenterPoint();
    const int modifiers =
        alone ? ui_controls::kAlt : ui_controls::kNoAccelerator;
    MoveMouse(from);
    PressMouse(ui_controls::DOWN, modifiers);
    constexpr int kSteps = 12;
    for (int step = 1; step <= kSteps; ++step) {
      MoveMouse(gfx::Point(from.x() + (to.x() - from.x()) * step / kSteps,
                           from.y() + (to.y() - from.y()) * step / kSteps));
      Wait(base::Milliseconds(20));
    }
    if (shot) {
      Wait(base::Milliseconds(300));
      SaveWindowScreenshot(browser(), shot);
    }
    PressMouse(ui_controls::UP, modifiers);
    Wait(base::Milliseconds(500));
  }

  static void MoveMouse(const gfx::Point& point) {
    base::RunLoop moved;
    ASSERT_TRUE(ui_controls::SendMouseMoveNotifyWhenDone(point.x(), point.y(),
                                                         moved.QuitClosure()));
    moved.Run();
  }

  static void PressMouse(int state, int modifiers) {
    base::RunLoop done;
    ASSERT_TRUE(ui_controls::SendMouseEventsNotifyWhenDone(
        ui_controls::LEFT, state, done.QuitClosure(), modifiers));
    done.Run();
  }

  static void Wait(base::TimeDelta delay) {
    base::RunLoop wait;
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
        FROM_HERE, wait.QuitClosure(), delay);
    wait.Run();
  }

  raw_ptr<tabs::TabInterface> parent_ = nullptr;
  raw_ptr<tabs::TabInterface> first_ = nullptr;
  raw_ptr<tabs::TabInterface> second_ = nullptr;
  raw_ptr<tabs::TabInterface> other_ = nullptr;
};

// A dragged parent takes its children with it, still nested under it, and
// only the parent stays selected.
IN_PROC_BROWSER_TEST_F(BranchDragBrowserTest,
                       MANUAL_ADraggedParentCarriesItsBranch) {
  StageBranch();
  DragBelow(parent_, other_, /*alone=*/false);
  EXPECT_EQ(Order(), (std::vector<tabs::TabInterface*>{other_, parent_, first_,
                                                       second_}));
  EXPECT_EQ(controller()->DepthFor(parent_), 0);
  EXPECT_EQ(controller()->DepthFor(first_), 1);
  EXPECT_EQ(controller()->DepthFor(second_), 1);
  EXPECT_EQ(tabs()->selection_model().GetListSelectionModel().size(), 1u);
  EXPECT_EQ(tabs()->GetActiveTab(), parent_);
}

// With Option held, the parent moves alone and its children move up a level.
IN_PROC_BROWSER_TEST_F(BranchDragBrowserTest,
                       MANUAL_AnOptionDragMovesTheParentAlone) {
  StageBranch();
  DragBelow(parent_, other_, /*alone=*/true);
  EXPECT_EQ(Order(), (std::vector<tabs::TabInterface*>{first_, second_, other_,
                                                       parent_}));
  EXPECT_EQ(controller()->DepthFor(first_), 0);
  EXPECT_EQ(controller()->DepthFor(second_), 0);
}

// A collapsed branch moves whole, hidden children included, and stays
// collapsed.
IN_PROC_BROWSER_TEST_F(BranchDragBrowserTest,
                       MANUAL_ACollapsedBranchMovesWhole) {
  StageBranch();
  const std::optional<PageId> id = controller()->IdFor(parent_);
  ASSERT_TRUE(id);
  ASSERT_TRUE(controller()->CollapsesBranches({*id}));
  controller()->ToggleBranches({*id});
  Wait(base::Milliseconds(500));
  ASSERT_TRUE(controller()->IsCollapsed(*id));

  DragBelow(parent_, other_, /*alone=*/false);
  EXPECT_EQ(Order(), (std::vector<tabs::TabInterface*>{other_, parent_, first_,
                                                       second_}));
  EXPECT_EQ(controller()->DepthFor(first_), 1);
  EXPECT_TRUE(controller()->IsCollapsed(*id));
}

// Moving the pointer left takes a tab out of its branch where it is, and
// moving it right nests a tab under the one above.
IN_PROC_BROWSER_TEST_F(BranchDragBrowserTest,
                       MANUAL_MovingADragSidewaysChangesTheDepth) {
  StageBranch();
  DragSideways(second_, -24, "sideways-drag-left");
  EXPECT_EQ(Order(), (std::vector<tabs::TabInterface*>{parent_, first_, second_,
                                                       other_}));
  EXPECT_EQ(controller()->DepthFor(first_), 1);
  EXPECT_EQ(controller()->DepthFor(second_), 0);

  DragSideways(other_, 24, "sideways-drag-right");
  EXPECT_EQ(controller()->DepthFor(other_), 1);
  EXPECT_TRUE(controller()->IsHeld(*controller()->IdFor(other_)));
}

// A child dragged straight down past another tab lands at that tab's level,
// not under it.
IN_PROC_BROWSER_TEST_F(BranchDragBrowserTest,
                       MANUAL_AStraightDragDoesNotNestUnderATabItPasses) {
  StageBranch();
  DragBelow(first_, other_, /*alone=*/false);
  EXPECT_EQ(Order(), (std::vector<tabs::TabInterface*>{parent_, second_, other_,
                                                       first_}));
  EXPECT_EQ(controller()->DepthFor(first_), 0);
}

// A drag that pulls a tab out of its branch past the window's left edge
// keeps the tab in this window.
IN_PROC_BROWSER_TEST_F(BranchDragBrowserTest,
                       MANUAL_DraggingPastTheLeftEdgeKeepsTheTab) {
  StageBranch();
  views::Widget* widget =
      BrowserView::GetBrowserViewForBrowser(browser())->GetWidget();
  gfx::Rect bounds = widget->GetWindowBoundsInScreen();
  bounds.set_x(160);
  widget->SetBounds(bounds);
  Wait(base::Milliseconds(500));
  const int edge = widget->GetWindowBoundsInScreen().x();
  const gfx::Point from = RowOf(second_)->GetBoundsInScreen().CenterPoint();
  Drag(second_, gfx::Point(edge - 40, from.y()), /*alone=*/false,
       "sideways-drag-past-the-edge");
  EXPECT_EQ(Order(), (std::vector<tabs::TabInterface*>{parent_, first_, second_,
                                                       other_}));
  EXPECT_EQ(controller()->DepthFor(second_), 0);
}

}  // namespace
}  // namespace uw
