// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/browser/tab_organization/tab_organization_controller.h"

#include <algorithm>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "base/run_loop.h"
#include "base/test/run_until.h"
#include "chrome/app/chrome_command_ids.h"
#include "chrome/browser/headless/test/headless_mode_browsertest.h"
#include "chrome/browser/prefs/session_startup_pref.h"
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
#include "chrome/test/base/ui_test_utils.h"
#include "components/prefs/pref_service.h"
#include "components/tabs/public/tab_group.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "ui/compositor/layer.h"
#include "ui/events/base_event_utils.h"
#include "ui/events/event.h"
#include "ui/gfx/animation/animation.h"
#include "ui/gfx/animation/animation_test_api.h"
#include "ui/gfx/geometry/point_f.h"
#include "ui/menus/simple_menu_model.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/controls/label.h"
#include "ui/views/test/button_test_api.h"
#include "ui/views/view.h"
#include "ui/views/widget/widget.h"
#include "url/gurl.h"
#include "uw/browser/tab_organization/window_screenshot.h"

namespace uw {
namespace {

class TabOrganizationControllerBrowserTest
    : public headless::HeadlessModeBrowserTest {
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
  tabs::TabInterface* OpenLinkFrom(
      tabs::TabInterface* source,
      const GURL& url = GURL("about:blank"),
      WindowOpenDisposition disposition =
          WindowOpenDisposition::NEW_BACKGROUND_TAB) {
    NavigateParams params(browser(), url, ui::PAGE_TRANSITION_LINK);
    params.disposition = disposition;
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

  // The views of one class in the window, top to bottom.
  std::vector<views::View*> ViewsOfClass(std::string_view name) {
    views::Widget* widget =
        BrowserView::GetBrowserViewForBrowser(browser())->GetWidget();
    widget->LayoutRootViewIfNecessary();
    std::vector<views::View*> found;
    const std::function<void(views::View*)> visit = [&](views::View* view) {
      if (std::string_view(view->GetClassName()) == name) {
        found.push_back(view);
      }
      for (views::View* child : view->children()) {
        visit(child);
      }
    };
    visit(widget->GetRootView());
    return found;
  }

  // The tab rows of the sidebar, top to bottom.
  std::vector<views::View*> TabRows() { return ViewsOfClass("TabView"); }

  std::vector<views::View*> VisibleRows() {
    std::vector<views::View*> rows = TabRows();
    std::erase_if(rows, [](views::View* row) { return !row->GetVisible(); });
    return rows;
  }

  // The child of `row` of the class `name`.
  views::View* PartOf(views::View* row, std::string_view name) {
    for (views::View* child : row->children()) {
      if (std::string_view(child->GetClassName()) == name) {
        return child;
      }
    }
    return nullptr;
  }

  void Collapse(tabs::TabInterface* tab) {
    ASSERT_TRUE(controller()->CollapsesBranches({Id(tab)}));
    controller()->ToggleBranches({Id(tab)});
    base::RunLoop().RunUntilIdle();
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
  EXPECT_EQ(Strip(),
            (std::vector<PageId>{Id(page), Id(first), Id(nested), Id(second)}));
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
  EXPECT_EQ(Strip(),
            (std::vector<PageId>{Id(page), Id(child), Id(sibling), Id(blank)}));
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
                       MoveToNewTaskGroupsTheBranchAboveTheScratchpad) {
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
  EXPECT_EQ(Strip(), (std::vector<PageId>{Id(page), Id(child), Id(other)}));
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
  // The Scratchpad takes it at its end, the foot of the strip.
  EXPECT_EQ(Strip(), (std::vector<PageId>{Id(page), Id(other), Id(child)}));
  ExpectStripMatchesTree();
}

// Tasks keep the order they were started in: a new one comes after every
// other task, wherever its tabs came from.
IN_PROC_BROWSER_TEST_F(TabOrganizationControllerBrowserTest,
                       ANewTaskComesAfterEveryOtherTask) {
  tabs::TabInterface* first = InitialTab();
  tabs::TabInterface* child = OpenLinkFrom(first);
  tabs::TabInterface* second = NewTab();
  tabs::TabInterface* loose = NewTab();
  const std::optional<tab_groups::TabGroupId> one =
      controller()->MoveToNewTask({Id(first)}, u"One");
  const std::optional<tab_groups::TabGroupId> two =
      controller()->MoveToNewTask({Id(second)}, u"Two");
  ASSERT_TRUE(one);
  ASSERT_TRUE(two);
  EXPECT_EQ(Strip(),
            (std::vector<PageId>{Id(first), Id(child), Id(second), Id(loose)}));

  const std::optional<tab_groups::TabGroupId> three =
      controller()->MoveToNewTask({Id(child)}, u"Three");
  ASSERT_TRUE(three);
  EXPECT_EQ(controller()->tree().Tasks(),
            (std::vector<std::string>{one->ToString(), two->ToString(),
                                      three->ToString()}));
  EXPECT_EQ(Strip(),
            (std::vector<PageId>{Id(first), Id(second), Id(child), Id(loose)}));
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
                       NativeGroupingIsAdoptedAndTheScratchpadStaysLast) {
  tabs::TabInterface* first = InitialTab();
  tabs::TabInterface* second = NewTab();
  tabs::TabInterface* third = NewTab();
  const tab_groups::TabGroupId group =
      tabs()->AddToNewGroup({tabs()->GetIndexOfTab(first)});
  base::RunLoop().RunUntilIdle();

  EXPECT_EQ(controller()->tree().Find(Id(first))->task, group.ToString());
  EXPECT_EQ(Strip(), (std::vector<PageId>{Id(first), Id(second), Id(third)}));
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
  EXPECT_TRUE(
      controller()->CanExecute(TabOrganizationController::kOutdent, selection));
  EXPECT_FALSE(
      controller()->CanExecute(TabOrganizationController::kNest, selection));
  EXPECT_FALSE(controller()->CanExecute(TabOrganizationController::kCloseBranch,
                                        selection));
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

IN_PROC_BROWSER_TEST_F(TabOrganizationControllerBrowserTest,
                       ScratchpadLabelSitsAboveUngroupedTabs) {
  const auto instant = gfx::AnimationTestApi::SetRichAnimationRenderMode(
      gfx::Animation::RichAnimationRenderMode::FORCE_DISABLED);
  tabs::TabInterface* page = InitialTab();
  std::vector<views::View*> rows = TabRows();
  ASSERT_EQ(rows.size(), 1u);
  EXPECT_EQ(rows[0]->y(), 24);

  // A task sits at the top. The label and the ungrouped tab come below it,
  // once the sidebar has grown the task's box around its tab.
  tabs::TabInterface* loose = NewTab();
  ASSERT_TRUE(controller()->MoveToNewTask({Id(page)}, u"Plan"));
  EXPECT_TRUE(base::test::RunUntil([&] {
    const std::vector<views::View*> tasks = ViewsOfClass("TabGroupView");
    rows = TabRows();
    return tasks.size() == 1u && tasks[0]->y() == 0 && rows.size() == 2u &&
           std::max(rows[0]->GetBoundsInScreen().y(),
                    rows[1]->GetBoundsInScreen().y()) ==
               tasks[0]->GetBoundsInScreen().bottom() + 2 + 24;
  }));

  // The sidebar drops the task header a moment after the tab leaves it.
  ASSERT_TRUE(controller()->MoveToScratchpad({Id(page)}));
  tabs()->CloseWebContentsAt(tabs()->GetIndexOfTab(loose),
                             TabCloseTypes::CLOSE_NONE);
  EXPECT_TRUE(base::test::RunUntil([&] {
    rows = TabRows();
    return rows.size() == 1u && rows[0]->y() == 24;
  }));
}

IN_PROC_BROWSER_TEST_F(TabOrganizationControllerBrowserTest,
                       ShortcutAndPlusButtonOpenATabInANewTask) {
  tabs::TabInterface* page = InitialTab();
  const std::optional<tab_groups::TabGroupId> plan =
      controller()->MoveToNewTask({Id(page)}, u"Plan");
  ASSERT_TRUE(plan);

  ASSERT_TRUE(chrome::ExecuteCommand(browser(), IDC_CREATE_NEW_TAB_GROUP));
  tabs::TabInterface* by_shortcut = tabs()->GetActiveTab();
  ASSERT_NE(by_shortcut, page);
  ASSERT_TRUE(by_shortcut->GetGroup());
  EXPECT_NE(by_shortcut->GetGroup(), plan);
  EXPECT_EQ(TitleOf(*by_shortcut->GetGroup()), u"New task");

  const std::vector<views::View*> foot =
      ViewsOfClass("VerticalTabStripBottomContainer");
  ASSERT_EQ(foot.size(), 1u);
  views::Button* plus = views::Button::AsButton(foot[0]->children().front());
  ASSERT_TRUE(plus);
  EXPECT_EQ(plus->GetTooltipText(), u"New task");
  views::test::ButtonTestApi(plus).NotifyDefaultMouseClick();
  tabs::TabInterface* by_button = tabs()->GetActiveTab();
  ASSERT_NE(by_button, by_shortcut);
  ASSERT_TRUE(by_button->GetGroup());
  EXPECT_NE(by_button->GetGroup(), plan);
  EXPECT_NE(by_button->GetGroup(), by_shortcut->GetGroup());
  EXPECT_EQ(TitleOf(*by_button->GetGroup()), u"New task");
  EXPECT_EQ(page->GetGroup(), plan);
  EXPECT_EQ(controller()->tree().Tasks().size(), 3u);
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
                       ACollapsedBranchHidesItsTabsAndCountsThem) {
  const auto instant = gfx::AnimationTestApi::SetRichAnimationRenderMode(
      gfx::Animation::RichAnimationRenderMode::FORCE_DISABLED);
  tabs::TabInterface* page = InitialTab();
  tabs::TabInterface* child = OpenLinkFrom(page);
  tabs::TabInterface* grandchild = OpenLinkFrom(child);
  tabs::TabInterface* after = NewTab();
  EXPECT_FALSE(controller()->CanExecute(
      TabOrganizationController::kToggleBranch, {Id(after)}));
  ASSERT_EQ(VisibleRows().size(), 4u);

  Collapse(page);
  EXPECT_TRUE(controller()->IsCollapsed(Id(page)));
  EXPECT_TRUE(controller()->IsHidden(Id(child)));
  EXPECT_TRUE(controller()->IsHidden(Id(grandchild)));
  EXPECT_FALSE(controller()->IsHidden(Id(page)));
  EXPECT_FALSE(controller()->IsHidden(Id(after)));
  std::vector<views::View*> rows;
  ASSERT_TRUE(base::test::RunUntil([&] {
    rows = VisibleRows();
    return rows.size() == 2u && rows[1]->GetBoundsInScreen().y() ==
                                    rows[0]->GetBoundsInScreen().bottom() + 2;
  }));
  auto* count = static_cast<views::Label*>(PartOf(rows[0], "HiddenCount"));
  ASSERT_TRUE(count);
  EXPECT_TRUE(count->GetVisible());
  EXPECT_EQ(count->GetText(), u"2");
  EXPECT_FALSE(PartOf(rows[1], "HiddenCount")->GetVisible());
  SaveWindowScreenshot(browser(), "branch-collapsed");

  tabs()->ActivateTabAt(tabs()->GetIndexOfTab(page));
  chrome::SelectNextTab(browser());
  EXPECT_EQ(tabs()->GetActiveTab(), after);
  chrome::SelectPreviousTab(browser());
  EXPECT_EQ(tabs()->GetActiveTab(), page);
  EXPECT_TRUE(controller()->IsCollapsed(Id(page)));
}

// A tab opened into a collapsed branch fades in on a layer of its own while
// it is already hidden. Its spinner must stay hidden once that layer goes.
IN_PROC_BROWSER_TEST_F(TabOrganizationControllerBrowserTest,
                       ATabLoadingInACollapsedBranchShowsNoSpinner) {
  ASSERT_TRUE(embedded_test_server()->Start());
  tabs::TabInterface* page = InitialTab();
  OpenLinkFrom(page);
  Collapse(page);
  OpenLinkFrom(page, embedded_test_server()->GetURL("/hung"));
  ASSERT_TRUE(base::test::RunUntil([&] {
    return !ViewsOfClass("ThrobberView").empty() &&
           std::ranges::none_of(TabRows(), [](views::View* row) {
             return row->layer() != nullptr;
           });
  }));
  SaveWindowScreenshot(browser(), "branch-collapsed-loading");
  for (views::View* row : TabRows()) {
    if (row->GetVisible()) {
      continue;
    }
    const std::function<void(views::View*)> expect_hidden =
        [&](views::View* view) {
          EXPECT_FALSE(view->layer() && view->layer()->IsVisible())
              << view->GetClassName();
          for (views::View* child : view->children()) {
            expect_hidden(child);
          }
        };
    expect_hidden(row);
  }
}

IN_PROC_BROWSER_TEST_F(TabOrganizationControllerBrowserTest,
                       TheChevronTakesTheFaviconsPlaceAndCollapsesTheBranch) {
  const auto instant = gfx::AnimationTestApi::SetRichAnimationRenderMode(
      gfx::Animation::RichAnimationRenderMode::FORCE_DISABLED);
  tabs::TabInterface* page = InitialTab();
  OpenLinkFrom(page);
  std::vector<views::View*> rows = VisibleRows();
  ASSERT_EQ(rows.size(), 2u);
  views::View* toggle = PartOf(rows[0], "BranchToggle");
  views::View* icon = PartOf(rows[0], "TabIcon");
  ASSERT_TRUE(toggle);
  ASSERT_TRUE(icon);
  EXPECT_FALSE(toggle->GetVisible());
  EXPECT_TRUE(icon->GetVisible());

  const ui::MouseEvent enter(ui::EventType::kMouseEntered, gfx::PointF(),
                             gfx::PointF(), ui::EventTimeForNow(), ui::EF_NONE,
                             ui::EF_NONE);
  rows[1]->OnMouseEntered(enter);
  rows = VisibleRows();
  EXPECT_FALSE(PartOf(rows[1], "BranchToggle")->GetVisible());
  rows[0]->OnMouseEntered(enter);
  rows = VisibleRows();
  EXPECT_TRUE(toggle->GetVisible());
  EXPECT_FALSE(icon->GetVisible());
  EXPECT_EQ(toggle->GetBoundsInScreen().CenterPoint(),
            icon->GetBoundsInScreen().CenterPoint());
  SaveWindowScreenshot(browser(), "branch-chevron");

  views::test::ButtonTestApi(views::Button::AsButton(toggle))
      .NotifyDefaultMouseClick();
  EXPECT_TRUE(controller()->IsCollapsed(Id(page)));
  EXPECT_EQ(tabs()->GetActiveTab(), page);
  ASSERT_TRUE(base::test::RunUntil([&] { return VisibleRows().size() == 1u; }));
  SaveWindowScreenshot(browser(), "branch-chevron-collapsed");

  rows[0]->OnMouseExited(
      ui::MouseEvent(ui::EventType::kMouseExited, gfx::PointF(), gfx::PointF(),
                     ui::EventTimeForNow(), ui::EF_NONE, ui::EF_NONE));
  VisibleRows();
  EXPECT_FALSE(toggle->GetVisible());
  EXPECT_TRUE(icon->GetVisible());
}

IN_PROC_BROWSER_TEST_F(TabOrganizationControllerBrowserTest,
                       SelectingAHiddenTabExpandsItsBranch) {
  tabs::TabInterface* page = InitialTab();
  tabs::TabInterface* child = OpenLinkFrom(page);
  tabs::TabInterface* grandchild = OpenLinkFrom(child);
  Collapse(child);
  Collapse(page);
  tabs()->ActivateTabAt(tabs()->GetIndexOfTab(grandchild));
  EXPECT_FALSE(controller()->IsCollapsed(Id(page)));
  EXPECT_FALSE(controller()->IsCollapsed(Id(child)));
  EXPECT_FALSE(controller()->IsHidden(Id(grandchild)));

  // Collapsing the branch the selected tab is in selects its top tab.
  Collapse(page);
  EXPECT_EQ(tabs()->GetActiveTab(), page);
  EXPECT_TRUE(controller()->IsHidden(Id(grandchild)));
}

IN_PROC_BROWSER_TEST_F(TabOrganizationControllerBrowserTest,
                       OnlyALinkOpenedInFrontExpandsACollapsedBranch) {
  tabs::TabInterface* page = InitialTab();
  OpenLinkFrom(page);
  Collapse(page);
  tabs::TabInterface* behind = OpenLinkFrom(page);
  EXPECT_TRUE(controller()->IsCollapsed(Id(page)));
  EXPECT_TRUE(controller()->IsHidden(Id(behind)));

  tabs::TabInterface* front = OpenLinkFrom(
      page, GURL("about:blank"), WindowOpenDisposition::NEW_FOREGROUND_TAB);
  EXPECT_EQ(tabs()->GetActiveTab(), front);
  EXPECT_EQ(controller()->DepthFor(front), 1);
  EXPECT_FALSE(controller()->IsCollapsed(Id(page)));
}

IN_PROC_BROWSER_TEST_F(TabOrganizationControllerBrowserTest,
                       ClosingACollapsedTabShowsItsTabsOneLevelUp) {
  tabs::TabInterface* page = InitialTab();
  tabs::TabInterface* child = OpenLinkFrom(page);
  tabs::TabInterface* grandchild = OpenLinkFrom(child);
  NewTab();
  Collapse(page);
  tabs()->CloseWebContentsAt(tabs()->GetIndexOfTab(page),
                             TabCloseTypes::CLOSE_NONE);
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(controller()->DepthFor(child), 0);
  EXPECT_FALSE(controller()->IsHidden(Id(child)));
  EXPECT_FALSE(controller()->IsHidden(Id(grandchild)));
}

IN_PROC_BROWSER_TEST_F(TabOrganizationControllerBrowserTest,
                       MovesAndUndoLeaveABranchCollapsed) {
  const auto instant = gfx::AnimationTestApi::SetRichAnimationRenderMode(
      gfx::Animation::RichAnimationRenderMode::FORCE_DISABLED);
  tabs::TabInterface* page = InitialTab();
  tabs::TabInterface* child = OpenLinkFrom(page);
  NewTab();
  Collapse(page);
  ASSERT_TRUE(controller()->MoveToNewTask({Id(page)}, u"Plan"));
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(child->GetGroup(), page->GetGroup());
  EXPECT_TRUE(controller()->IsCollapsed(Id(page)));
  EXPECT_TRUE(controller()->IsHidden(Id(child)));
  ASSERT_TRUE(base::test::RunUntil([&] { return VisibleRows().size() == 2u; }));

  controller()->Undo();
  base::RunLoop().RunUntilIdle();
  EXPECT_FALSE(page->GetGroup());
  EXPECT_TRUE(controller()->IsCollapsed(Id(page)));
}

IN_PROC_BROWSER_TEST_F(TabOrganizationControllerBrowserTest,
                       TheTabMenuSaysWhichWayTheBranchGoes) {
  tabs::TabInterface* page = InitialTab();
  OpenLinkFrom(page);
  const auto label = [&] {
    ui::SimpleMenuModel menu(nullptr);
    controller()->AppendCommands(&menu, {Id(page)});
    return menu.GetLabelAt(
        menu.GetIndexOfCommandId(TabOrganizationController::kToggleBranch)
            .value());
  };
  EXPECT_EQ(label(), u"Collapse branch");
  Collapse(page);
  EXPECT_EQ(label(), u"Expand branch");
}

IN_PROC_BROWSER_TEST_F(TabOrganizationControllerBrowserTest,
                       HorizontalTabsCollapseNothing) {
  const auto vertical = [&](bool on) {
    browser()->GetProfile()->GetPrefs()->SetBoolean(
        ::prefs::kVerticalTabsEnabled, on);
    base::RunLoop().RunUntilIdle();
  };
  tabs::TabInterface* page = InitialTab();
  tabs::TabInterface* child = OpenLinkFrom(page);
  Collapse(page);
  vertical(false);
  EXPECT_FALSE(controller()->IsHidden(Id(child)));
  EXPECT_FALSE(controller()->CanExecute(
      TabOrganizationController::kToggleBranch, {Id(page)}));
  vertical(true);
  EXPECT_TRUE(controller()->IsHidden(Id(child)));

  // Selecting a tab that the sidebar would hide expands its branch.
  vertical(false);
  chrome::SelectNextTab(browser());
  EXPECT_EQ(tabs()->GetActiveTab(), child);
  EXPECT_FALSE(controller()->IsCollapsed(Id(page)));
}

IN_PROC_BROWSER_TEST_F(TabOrganizationControllerBrowserTest,
                       PRE_ACollapsedBranchStaysCollapsedAfterARestart) {
  SessionStartupPref::SetStartupPref(
      browser()->GetProfile(), SessionStartupPref(SessionStartupPref::LAST));
  ASSERT_TRUE(
      ui_test_utils::NavigateToURL(browser(), GURL("chrome://version/")));
  tabs::TabInterface* page = InitialTab();
  tabs::TabInterface* child =
      OpenLinkFrom(page, GURL("chrome://version/?child"));
  ASSERT_TRUE(content::WaitForLoadStop(child->GetContents()));
  Collapse(page);
  // Lets the session take the tabs' data before the browser quits.
  base::RunLoop().RunUntilIdle();
}

IN_PROC_BROWSER_TEST_F(TabOrganizationControllerBrowserTest,
                       ACollapsedBranchStaysCollapsedAfterARestart) {
  const auto tab_at = [&](std::string_view url) -> tabs::TabInterface* {
    for (int i = 0; i < tabs()->count(); ++i) {
      tabs::TabInterface* tab = tabs()->GetTabAtIndex(i);
      if (tab->GetContents()->GetVisibleURL() == GURL(url)) {
        return tab;
      }
    }
    return nullptr;
  };
  tabs::TabInterface* page = nullptr;
  tabs::TabInterface* child = nullptr;
  ASSERT_TRUE(base::test::RunUntil([&] {
    page = tab_at("chrome://version/");
    child = tab_at("chrome://version/?child");
    return page && child && controller()->DepthFor(child) == 1;
  }));
  EXPECT_TRUE(controller()->IsCollapsed(Id(page)));
  EXPECT_TRUE(controller()->IsHidden(Id(child)));
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
