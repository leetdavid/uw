// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/browser/tab_organization/tab_organization_controller.h"

#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "base/run_loop.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/navigator/browser_navigator.h"
#include "chrome/browser/ui/navigator/browser_navigator_params.h"
#include "chrome/browser/ui/tabs/tab_enums.h"
#include "chrome/browser/ui/tabs/tab_group_model.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/common/pref_names.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "components/prefs/pref_service.h"
#include "components/tabs/public/tab_group.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "ui/gfx/animation/animation.h"
#include "ui/gfx/animation/animation_test_api.h"
#include "ui/views/view.h"
#include "ui/views/widget/widget.h"
#include "url/gurl.h"

namespace uw {
namespace {

class TabOrganizationControllerBrowserTest : public InProcessBrowserTest {
 protected:
  TabOrganizationController* controller() {
    return TabOrganizationController::From(browser());
  }

  TabStripModel* tabs() { return browser()->tab_strip_model(); }

  tabs::TabInterface* InitialTab() { return tabs()->GetTabAtIndex(0); }

  // Opens a blank tab the way Cmd+T does and lets the strip settle.
  tabs::TabInterface* NewTab() {
    chrome::NewTab(browser(), NewTabTypes::kNewTabCommand);
    tabs::TabInterface* tab = tabs()->GetActiveTab();
    base::RunLoop().RunUntilIdle();
    return tab;
  }

  // Opens a background tab the way Cmd-clicking a link in `source` does.
  tabs::TabInterface* OpenLinkFrom(tabs::TabInterface* source) {
    NavigateParams params(browser(), GURL("about:blank"),
                          ui::PAGE_TRANSITION_LINK);
    params.disposition = WindowOpenDisposition::NEW_BACKGROUND_TAB;
    params.source_contents = source->GetContents();
    Navigate(&params);
    tabs::TabInterface* tab =
        tabs()->GetTabForWebContents(params.navigated_or_inserted_contents);
    base::RunLoop().RunUntilIdle();
    return tab;
  }

  PageId Id(tabs::TabInterface* tab) {
    const std::optional<PageId> id = controller()->IdFor(tab);
    EXPECT_TRUE(id);
    return id.value_or(PageId());
  }

  std::vector<PageId> Strip() {
    std::vector<PageId> ids;
    for (int i = 0; i < tabs()->count(); ++i) {
      if (const std::optional<PageId> id =
              controller()->IdFor(tabs()->GetTabAtIndex(i))) {
        ids.push_back(*id);
      }
    }
    return ids;
  }

  // The tree and the strip must agree on order and on task membership.
  void ExpectStripMatchesTree() {
    std::vector<PageId> tree;
    for (const Page& page : controller()->tree().pages()) {
      tree.push_back(page.id);
      const std::optional<tab_groups::TabGroupId> group =
          controller()->TabFor(page.id)->GetGroup();
      EXPECT_EQ(page.task, group ? group->ToString() : std::string());
    }
    EXPECT_EQ(tree, Strip());
  }

  // The tab rows of the sidebar, top to bottom.
  std::vector<views::View*> TabRows() {
    views::Widget* widget =
        BrowserView::GetBrowserViewForBrowser(browser())->GetWidget();
    widget->LayoutRootViewIfNecessary();
    std::vector<views::View*> rows;
    const std::function<void(views::View*)> visit = [&](views::View* view) {
      if (std::string_view(view->GetClassName()) == "TabView") {
        rows.push_back(view);
      }
      for (views::View* child : view->children()) {
        visit(child);
      }
    };
    visit(widget->GetRootView());
    return rows;
  }

  std::u16string TitleOf(const tab_groups::TabGroupId& group) {
    return tabs()->group_model()->GetTabGroup(group)->visual_data()->title();
  }
};

IN_PROC_BROWSER_TEST_F(TabOrganizationControllerBrowserTest,
                       TracksEveryUnpinnedTab) {
  ASSERT_TRUE(controller());
  EXPECT_EQ(controller()->tree().pages().size(), 1u);
  tabs::TabInterface* tab = NewTab();
  EXPECT_EQ(controller()->tree().pages().size(), 2u);
  EXPECT_EQ(controller()->DepthFor(tab), 0);
  ExpectStripMatchesTree();
}

IN_PROC_BROWSER_TEST_F(TabOrganizationControllerBrowserTest,
                       LinkOpensAsLastChildOfItsSource) {
  tabs::TabInterface* page = InitialTab();
  tabs::TabInterface* first = OpenLinkFrom(page);
  tabs::TabInterface* second = OpenLinkFrom(page);
  tabs::TabInterface* nested = OpenLinkFrom(first);
  EXPECT_EQ(controller()->DepthFor(first), 1);
  EXPECT_EQ(controller()->DepthFor(second), 1);
  EXPECT_EQ(controller()->DepthFor(nested), 2);
  EXPECT_EQ(Strip(), (std::vector<PageId>{Id(page), Id(first), Id(nested),
                                           Id(second)}));
  ExpectStripMatchesTree();
}

IN_PROC_BROWSER_TEST_F(TabOrganizationControllerBrowserTest,
                       BlankNewTabOpensBelowTheActiveBranch) {
  tabs::TabInterface* page = InitialTab();
  tabs::TabInterface* child = OpenLinkFrom(page);
  tabs::TabInterface* blank = NewTab();
  EXPECT_EQ(controller()->DepthFor(blank), 0);
  EXPECT_EQ(Strip(), (std::vector<PageId>{Id(page), Id(child), Id(blank)}));

  tabs()->ActivateTabAt(tabs()->GetIndexOfTab(page));
  tabs::TabInterface* sibling = NewTab();
  EXPECT_EQ(Strip(), (std::vector<PageId>{Id(page), Id(child), Id(sibling),
                                           Id(blank)}));
  ExpectStripMatchesTree();
}

IN_PROC_BROWSER_TEST_F(TabOrganizationControllerBrowserTest,
                       ClosingAParentMovesItsChildrenUpOneLevel) {
  tabs::TabInterface* page = InitialTab();
  tabs::TabInterface* child = OpenLinkFrom(page);
  tabs::TabInterface* grandchild = OpenLinkFrom(child);
  tabs()->CloseWebContentsAt(tabs()->GetIndexOfTab(child),
                             TabCloseTypes::CLOSE_NONE);
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(controller()->DepthFor(grandchild), 1);
  EXPECT_EQ(Strip(), (std::vector<PageId>{Id(page), Id(grandchild)}));
}

IN_PROC_BROWSER_TEST_F(TabOrganizationControllerBrowserTest,
                       MoveToNewTaskGroupsTheBranchBelowTheScratchpad) {
  tabs::TabInterface* page = InitialTab();
  tabs::TabInterface* child = OpenLinkFrom(page);
  tabs::TabInterface* other = NewTab();
  content::WebContents* contents = page->GetContents();

  const std::optional<tab_groups::TabGroupId> group =
      controller()->MoveToNewTask({Id(page)}, u"Plan Lisbon trip");
  ASSERT_TRUE(group);
  EXPECT_EQ(page->GetGroup(), group);
  EXPECT_EQ(child->GetGroup(), group);
  EXPECT_FALSE(other->GetGroup());
  EXPECT_EQ(TitleOf(*group), u"Plan Lisbon trip");
  EXPECT_EQ(controller()->DepthFor(child), 1);
  EXPECT_EQ(Strip(), (std::vector<PageId>{Id(other), Id(page), Id(child)}));
  EXPECT_EQ(page->GetContents(), contents);
  EXPECT_EQ(tabs()->GetActiveTab(), other);
  ExpectStripMatchesTree();
}

IN_PROC_BROWSER_TEST_F(TabOrganizationControllerBrowserTest,
                       MoveToScratchpadUngroupsAndDetachesTheBranch) {
  tabs::TabInterface* page = InitialTab();
  tabs::TabInterface* child = OpenLinkFrom(page);
  tabs::TabInterface* other = NewTab();
  const std::optional<tab_groups::TabGroupId> group =
      controller()->MoveToNewTask({Id(page)}, u"Plan");
  ASSERT_TRUE(group);

  EXPECT_TRUE(controller()->MoveToScratchpad({Id(child)}));
  EXPECT_FALSE(child->GetGroup());
  EXPECT_EQ(page->GetGroup(), group);
  EXPECT_EQ(controller()->DepthFor(child), 0);
  EXPECT_EQ(Strip(), (std::vector<PageId>{Id(other), Id(child), Id(page)}));
  ExpectStripMatchesTree();
}

IN_PROC_BROWSER_TEST_F(TabOrganizationControllerBrowserTest,
                       MoveToTaskAppendsToAnExistingGroup) {
  tabs::TabInterface* first = InitialTab();
  tabs::TabInterface* second = NewTab();
  const std::optional<tab_groups::TabGroupId> one =
      controller()->MoveToNewTask({Id(first)}, u"One");
  const std::optional<tab_groups::TabGroupId> two =
      controller()->MoveToNewTask({Id(second)}, u"Two");
  ASSERT_TRUE(one);
  ASSERT_TRUE(two);

  EXPECT_TRUE(controller()->MoveToTask({Id(first)}, *two));
  EXPECT_EQ(first->GetGroup(), two);
  EXPECT_EQ(tabs()->group_model()->ListTabGroups().size(), 1u);
  EXPECT_EQ(Strip(), (std::vector<PageId>{Id(second), Id(first)}));
  ExpectStripMatchesTree();
}

IN_PROC_BROWSER_TEST_F(TabOrganizationControllerBrowserTest,
                       UndoRestoresPlacementAndRecreatesAVanishedTask) {
  tabs::TabInterface* page = InitialTab();
  const std::optional<tab_groups::TabGroupId> group =
      controller()->MoveToNewTask({Id(page)}, u"Plan");
  ASSERT_TRUE(group);
  ASSERT_TRUE(controller()->MoveToScratchpad({Id(page)}));
  EXPECT_TRUE(tabs()->group_model()->ListTabGroups().empty());

  controller()->Undo();
  EXPECT_EQ(page->GetGroup(), group);
  EXPECT_EQ(TitleOf(*group), u"Plan");

  controller()->Undo();
  EXPECT_FALSE(page->GetGroup());
  EXPECT_FALSE(controller()->CanUndo());
  ExpectStripMatchesTree();
}

IN_PROC_BROWSER_TEST_F(TabOrganizationControllerBrowserTest,
                       UndoKeepsTabsOpenedSince) {
  tabs::TabInterface* page = InitialTab();
  const std::optional<tab_groups::TabGroupId> group =
      controller()->MoveToNewTask({Id(page)}, u"Plan");
  ASSERT_TRUE(group);
  tabs::TabInterface* opened = OpenLinkFrom(page);
  EXPECT_EQ(opened->GetGroup(), group);

  controller()->Undo();
  EXPECT_FALSE(page->GetGroup());
  EXPECT_FALSE(opened->GetGroup());
  EXPECT_EQ(controller()->DepthFor(opened), 1);
  EXPECT_EQ(tabs()->count(), 2);
  ExpectStripMatchesTree();
}

IN_PROC_BROWSER_TEST_F(TabOrganizationControllerBrowserTest,
                       NativeGroupingIsAdoptedAndScratchpadStaysOnTop) {
  tabs::TabInterface* first = InitialTab();
  tabs::TabInterface* second = NewTab();
  tabs::TabInterface* third = NewTab();
  const tab_groups::TabGroupId group =
      tabs()->AddToNewGroup({tabs()->GetIndexOfTab(first)});
  base::RunLoop().RunUntilIdle();

  EXPECT_EQ(controller()->tree().Find(Id(first))->task, group.ToString());
  EXPECT_EQ(Strip(), (std::vector<PageId>{Id(second), Id(third), Id(first)}));
  ExpectStripMatchesTree();
}

IN_PROC_BROWSER_TEST_F(TabOrganizationControllerBrowserTest,
                       NestAndOutdentAreUndoable) {
  tabs::TabInterface* first = InitialTab();
  tabs::TabInterface* second = NewTab();
  EXPECT_FALSE(controller()->Nest(Id(first)));
  EXPECT_FALSE(controller()->CanUndo());

  EXPECT_TRUE(controller()->Nest(Id(second)));
  EXPECT_EQ(controller()->DepthFor(second), 1);
  EXPECT_TRUE(controller()->Outdent(Id(second)));
  EXPECT_EQ(controller()->DepthFor(second), 0);
  controller()->Undo();
  EXPECT_EQ(controller()->DepthFor(second), 1);
  ExpectStripMatchesTree();
}

IN_PROC_BROWSER_TEST_F(TabOrganizationControllerBrowserTest,
                       CloseBranchesClosesDescendants) {
  tabs::TabInterface* keep = InitialTab();
  tabs::TabInterface* page = NewTab();
  tabs::TabInterface* child = OpenLinkFrom(page);
  OpenLinkFrom(child);
  ASSERT_EQ(tabs()->count(), 4);

  controller()->CloseBranches({Id(page)});
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(tabs()->count(), 1);
  EXPECT_EQ(Strip(), (std::vector<PageId>{Id(keep)}));
}

IN_PROC_BROWSER_TEST_F(TabOrganizationControllerBrowserTest,
                       PinningATabTakesItOutOfTheTree) {
  tabs::TabInterface* page = InitialTab();
  tabs::TabInterface* child = OpenLinkFrom(page);
  tabs()->SetTabPinned(tabs()->GetIndexOfTab(page), true);
  base::RunLoop().RunUntilIdle();
  EXPECT_FALSE(controller()->IdFor(page));
  EXPECT_EQ(controller()->DepthFor(child), 0);
  EXPECT_EQ(controller()->tree().pages().size(), 1u);
}

IN_PROC_BROWSER_TEST_F(TabOrganizationControllerBrowserTest,
                       MenuCommandsFollowTheSelection) {
  tabs::TabInterface* page = InitialTab();
  tabs::TabInterface* child = OpenLinkFrom(page);
  const std::vector<PageId> selection = controller()->SelectionFor(child);
  EXPECT_EQ(selection, (std::vector<PageId>{Id(child)}));
  EXPECT_TRUE(controller()->CanExecute(TabOrganizationController::kOutdent,
                                       selection));
  EXPECT_FALSE(
      controller()->CanExecute(TabOrganizationController::kNest, selection));
  EXPECT_FALSE(controller()->CanExecute(
      TabOrganizationController::kCloseBranch, selection));
  EXPECT_FALSE(
      controller()->CanExecute(TabOrganizationController::kUndo, selection));

  controller()->Execute(TabOrganizationController::kNewTask, selection);
  EXPECT_TRUE(child->GetGroup());
  EXPECT_FALSE(page->GetGroup());
  EXPECT_TRUE(
      controller()->CanExecute(TabOrganizationController::kUndo, selection));
}

IN_PROC_BROWSER_TEST_F(TabOrganizationControllerBrowserTest,
                       NestedTabsAreIndentedInTheSidebar) {
  const auto instant = gfx::AnimationTestApi::SetRichAnimationRenderMode(
      gfx::Animation::RichAnimationRenderMode::FORCE_DISABLED);
  tabs::TabInterface* page = InitialTab();
  tabs::TabInterface* child = OpenLinkFrom(page);
  OpenLinkFrom(child);
  std::vector<views::View*> rows = TabRows();
  ASSERT_EQ(rows.size(), 3u);
  const int left = rows[0]->GetBoundsInScreen().x();
  EXPECT_EQ(rows[1]->GetBoundsInScreen().x(), left + 16);
  EXPECT_EQ(rows[2]->GetBoundsInScreen().x(), left + 32);
  EXPECT_EQ(rows[2]->GetBoundsInScreen().right(),
            rows[0]->GetBoundsInScreen().right());

  ASSERT_TRUE(controller()->MoveToNewTask({Id(page)}, u"Plan"));
  base::RunLoop().RunUntilIdle();
  rows = TabRows();
  ASSERT_EQ(rows.size(), 3u);
  EXPECT_EQ(rows[1]->GetBoundsInScreen().x(),
            rows[0]->GetBoundsInScreen().x() + 16);

  ASSERT_TRUE(controller()->Outdent(Id(child)));
  base::RunLoop().RunUntilIdle();
  rows = TabRows();
  EXPECT_EQ(rows[1]->GetBoundsInScreen().x(), rows[0]->GetBoundsInScreen().x());
}

// Switching tab orientation rebuilds the sidebar views. The window-owned
// tree must not depend on them.
IN_PROC_BROWSER_TEST_F(TabOrganizationControllerBrowserTest,
                       OrientationTogglePreservesTree) {
  tabs::TabInterface* page = InitialTab();
  tabs::TabInterface* child = OpenLinkFrom(page);
  ASSERT_TRUE(controller()->MoveToNewTask({Id(page)}, u"Keep"));
  const PageTree before = controller()->tree();

  PrefService* prefs = browser()->GetProfile()->GetPrefs();
  prefs->SetBoolean(::prefs::kVerticalTabsEnabled, false);
  base::RunLoop().RunUntilIdle();
  prefs->SetBoolean(::prefs::kVerticalTabsEnabled, true);
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(controller()->tree(), before);
  EXPECT_EQ(controller()->DepthFor(child), 1);
}

IN_PROC_BROWSER_TEST_F(TabOrganizationControllerBrowserTest,
                       RapidTabInsertionKeepsStripAndTreeAligned) {
  for (int i = 0; i < 5; ++i) {
    chrome::NewTab(browser(), NewTabTypes::kNewTabCommand);
  }
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(controller()->tree().pages().size(), 6u);
  ExpectStripMatchesTree();
}

IN_PROC_BROWSER_TEST_F(TabOrganizationControllerBrowserTest,
                       WindowCloseWithTasksTearsDownCleanly) {
  tabs::TabInterface* page = InitialTab();
  OpenLinkFrom(page);
  ASSERT_TRUE(controller()->MoveToNewTask({Id(page)}, u"Keep"));
  CloseAllBrowsers();
}

}  // namespace
}  // namespace uw
