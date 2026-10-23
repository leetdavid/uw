// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/browser/tab_organization/tab_organization_controller.h"

#include <algorithm>
#include <climits>
#include <functional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "base/run_loop.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "base/test/run_until.h"
#include "base/uuid.h"
#include "chrome/app/chrome_command_ids.h"
#include "chrome/browser/headless/test/headless_mode_browsertest.h"
#include "chrome/browser/prefs/session_startup_pref.h"
#include "chrome/browser/profiles/keep_alive/profile_keep_alive_types.h"
#include "chrome/browser/profiles/keep_alive/scoped_profile_keep_alive.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/sessions/session_restore_test_helper.h"
#include "chrome/browser/sessions/session_service.h"
#include "chrome/browser/sessions/session_service_factory.h"
#include "chrome/browser/sessions/session_service_test_helper.h"
#include "chrome/browser/tab_group_sync/tab_group_sync_service_factory.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_window/public/browser_window_features.h"
#include "chrome/browser/ui/browser_window/public/global_browser_collection.h"
#include "chrome/browser/ui/navigator/browser_navigator.h"
#include "chrome/browser/ui/navigator/browser_navigator_params.h"
#include "chrome/browser/ui/tabs/tab_enums.h"
#include "chrome/browser/ui/tabs/tab_group_model.h"
#include "chrome/browser/ui/tabs/tab_menu_model.h"
#include "chrome/browser/ui/session_crashed_bubble.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/toolbar/app_menu_model.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/session_crashed_bubble_view.h"
#include "chrome/browser/ui/views/tabs/common/tab_group_header_view.h"
#include "chrome/browser/ui/views/tabs/tab/tab_context_menu_controller.h"
#include "chrome/common/pref_names.h"
#include "chrome/grit/generated_resources.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/keep_alive_registry/keep_alive_types.h"
#include "components/keep_alive_registry/scoped_keep_alive.h"
#include "components/prefs/pref_service.h"
#include "components/saved_tab_groups/public/saved_tab_group.h"
#include "components/saved_tab_groups/public/saved_tab_group_tab.h"
#include "components/saved_tab_groups/public/tab_group_sync_service.h"
#include "components/split_tabs/split_tab_visual_data.h"
#include "components/tabs/public/tab_group.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "ui/base/hit_test.h"
#include "ui/compositor/layer.h"
#include "ui/events/base_event_utils.h"
#include "ui/events/event.h"
#include "ui/gfx/animation/animation.h"
#include "ui/gfx/animation/animation_test_api.h"
#include "ui/gfx/geometry/point_f.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/menus/simple_menu_model.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/views/bubble/bubble_dialog_delegate_view.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/controls/button/checkbox.h"
#include "ui/views/controls/button/label_button.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/styled_label.h"
#include "ui/views/test/button_test_api.h"
#include "ui/views/view.h"
#include "ui/views/widget/widget.h"
#include "url/gurl.h"
#include "uw/browser/tab_organization/ai_connection.h"
#include "uw/browser/tab_organization/commands.h"
#include "uw/browser/tab_organization/window_screenshot.h"
#include "uw/browser/ui/views/tab_organization/scratchpad_heading.h"
#include "uw/browser/ui/views/tab_organization/window_commands.h"

namespace uw {
namespace {

// Runs a tab's menu commands on the strip, as the tab strip does.
class TabMenuDelegate : public TabContextMenuController::Delegate {
 public:
  explicit TabMenuDelegate(TabStripModel* tabs) : tabs_(tabs) {}

  bool IsContextMenuCommandChecked(
      TabStripModel::ContextMenuCommand command) override {
    return false;
  }
  bool IsContextMenuCommandEnabled(
      tabs::TabInterface* tab,
      TabStripModel::ContextMenuCommand command) override {
    return tabs_->IsContextMenuCommandEnabled(tabs_->GetIndexOfTab(tab),
                                              command);
  }
  bool IsContextMenuCommandAlerted(
      TabStripModel::ContextMenuCommand command) override {
    return false;
  }
  void ExecuteContextMenuCommand(tabs::TabInterface* tab,
                                 TabStripModel::ContextMenuCommand command,
                                 int event_flags) override {
    tabs_->ExecuteContextMenuCommand(tabs_->GetIndexOfTab(tab), command);
  }
  bool GetContextMenuAccelerator(int command,
                                 ui::Accelerator* accelerator) override {
    return false;
  }

 private:
  const raw_ptr<TabStripModel> tabs_;
};

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

  // A tree to bring back after a restart. In the Scratchpad: `a`, with `b`
  // and then `d` under it, and `c` under `b`, which is collapsed. Above it, a
  // task the user named "Plan": `e`, with `f` under it.
  void BuildTreeToRestore() {
    SessionStartupPref::SetStartupPref(
        browser()->GetProfile(), SessionStartupPref(SessionStartupPref::LAST));
    const auto url = [](std::string_view name) {
      return GURL(std::string("chrome://version/?") + std::string(name));
    };
    ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), url("a")));
    tabs::TabInterface* a = InitialTab();
    tabs::TabInterface* b = OpenLinkFrom(a, url("b"));
    OpenLinkFrom(b, url("c"));
    OpenLinkFrom(a, url("d"));
    tabs::TabInterface* e = OpenLinkFrom(a, url("e"));
    ASSERT_TRUE(controller()->MoveToNewTask({Id(e)}, u"Plan"));
    OpenLinkFrom(e, url("f"));
    // Session restore skips a tab with no committed page.
    ASSERT_TRUE(base::test::RunUntil([&] {
      for (int i = 0; i < tabs()->count(); ++i) {
        content::WebContents* contents = tabs()->GetTabAtIndex(i)->GetContents();
        if (contents->GetLastCommittedURL() != contents->GetVisibleURL()) {
          return false;
        }
      }
      return true;
    }));
    Collapse(b);
    // Lets the session take the tabs' data.
    base::RunLoop().RunUntilIdle();
  }

  // Opens `a` with `b` under it and waits until both pages are committed,
  // which session restore needs.
  void OpenAWithBUnderIt() {
    ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(),
                                             GURL("chrome://version/?a")));
    tabs::TabInterface* b =
        OpenLinkFrom(InitialTab(), GURL("chrome://version/?b"));
    ASSERT_TRUE(base::test::RunUntil([&] {
      return b->GetContents()->GetLastCommittedURL() ==
             GURL("chrome://version/?b");
    }));
    base::RunLoop().RunUntilIdle();
  }

  // The window's tree in tab order: each named tab's name, its depth, `*` when its
  // branch is collapsed, and its task's title with who named it.
  std::string TreeShape() {
    std::string shape;
    for (int i = 0; i < tabs()->count(); ++i) {
      tabs::TabInterface* tab = tabs()->GetTabAtIndex(i);
      const std::optional<PageId> id = controller()->IdFor(tab);
      const std::string name(tab->GetContents()->GetVisibleURL().query());
      // Skips the blank tab the test opens beside the restored ones.
      if (!id || name.empty()) {
        continue;
      }
      shape += shape.empty() ? "" : " ";
      shape += name +
               base::NumberToString(controller()->DepthFor(tab));
      if (controller()->IsCollapsed(*id)) {
        shape += "*";
      }
      if (const std::optional<tab_groups::TabGroupId> group = tab->GetGroup()) {
        const std::string task = group->ToString();
        shape += "@" + base::UTF16ToUTF8(controller()->TaskTitle(task)) +
                 (controller()->HasGeneratedName(task) ? "/uw" : "/you");
      }
    }
    return shape;
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
                       ASplitRowSitsWhereItsShallowerTabWould) {
  const auto instant = gfx::AnimationTestApi::SetRichAnimationRenderMode(
      gfx::Animation::RichAnimationRenderMode::FORCE_DISABLED);
  tabs::TabInterface* page = InitialTab();
  tabs::TabInterface* child = OpenLinkFrom(page);
  tabs::TabInterface* grandchild = OpenLinkFrom(child);
  OpenLinkFrom(child);

  tabs()->ActivateTabAt(tabs()->GetIndexOfTab(child));
  tabs()->ExecuteAddToNewSplitCommand(tabs()->GetIndexOfTab(grandchild),
                                      split_tabs::SplitTabLayout::kSideBySide);
  ASSERT_TRUE(child->GetSplit());
  base::RunLoop().RunUntilIdle();

  const std::vector<views::View*> splits = ViewsOfClass("SplitTabView");
  // Top to bottom, and the split's halves left to right.
  std::vector<views::View*> rows = TabRows();
  std::ranges::sort(rows, {}, [](views::View* row) {
    const gfx::Rect bounds = row->GetBoundsInScreen();
    return std::pair(bounds.y(), bounds.x());
  });
  ASSERT_EQ(splits.size(), 1u);
  ASSERT_EQ(rows.size(), 4u);
  const int left = rows[0]->GetBoundsInScreen().x();
  EXPECT_EQ(splits[0]->GetBoundsInScreen().x(), left + 16);
  // The pair's other child still nests under it.
  EXPECT_EQ(rows[3]->GetBoundsInScreen().x(), left + 32);

  tabs()->ActivateTabAt(tabs()->GetIndexOfTab(page));
  Collapse(page);
  EXPECT_TRUE(base::test::RunUntil(
      [&] { return !ViewsOfClass("SplitTabView")[0]->GetVisible(); }));
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
                       TheShortcutOpensATabInANewTask) {
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
  EXPECT_EQ(page->GetGroup(), plan);
  EXPECT_EQ(controller()->tree().Tasks().size(), 2u);
}

// Tabs are always vertical. A horizontal choice saved before uw turned
// horizontal tabs off goes at the next launch, and no menu offers it again.
IN_PROC_BROWSER_TEST_F(TabOrganizationControllerBrowserTest,
                       PRE_TabsComeBackVerticalAfterAHorizontalChoice) {
  browser()->GetProfile()->GetPrefs()->SetBoolean(::prefs::kVerticalTabsEnabled,
                                                  false);
}

IN_PROC_BROWSER_TEST_F(TabOrganizationControllerBrowserTest,
                       TabsComeBackVerticalAfterAHorizontalChoice) {
  EXPECT_FALSE(browser()->GetProfile()->GetPrefs()->HasPrefPath(
      ::prefs::kVerticalTabsEnabled));
  EXPECT_TRUE(BrowserView::GetBrowserViewForBrowser(browser())
                  ->ShouldDrawVerticalTabStrip());
  EXPECT_FALSE(chrome::IsCommandEnabled(browser(), IDC_TOGGLE_VERTICAL_TABS));
  TabMenuModel tab_menu(
      nullptr, browser()->GetFeatures().tab_menu_model_delegate(), tabs(), 0);
  EXPECT_FALSE(
      tab_menu.GetIndexOfCommandId(TabStripModel::CommandToggleVertical));
  ToolsMenuModel more_tools(nullptr, browser());
  EXPECT_FALSE(more_tools.GetIndexOfCommandId(IDC_TOGGLE_VERTICAL_TABS));
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

// A kept tab's row ends in an anchor, after a collapsed branch's count. A
// click on the anchor lets the tab go.
IN_PROC_BROWSER_TEST_F(TabOrganizationControllerBrowserTest,
                       AKeptTabsRowEndsInAnAnchor) {
  const auto instant = gfx::AnimationTestApi::SetRichAnimationRenderMode(
      gfx::Animation::RichAnimationRenderMode::FORCE_DISABLED);
  tabs::TabInterface* page = InitialTab();
  OpenLinkFrom(page);
  EXPECT_TRUE(controller()->KeepsTabs({Id(page)}));
  controller()->Execute(TabOrganizationController::kKeepHere, {Id(page)});
  EXPECT_TRUE(controller()->IsKept(Id(page)));
  EXPECT_FALSE(controller()->KeepsTabs({Id(page)}));
  Collapse(page);
  ASSERT_TRUE(base::test::RunUntil([&] { return VisibleRows().size() == 1u; }));
  views::View* row = VisibleRows()[0];
  views::View* anchor = PartOf(row, "KeptMark");
  views::View* count = PartOf(row, "HiddenCount");
  views::View* title = PartOf(row, "TabTitle");
  ASSERT_TRUE(anchor);
  ASSERT_TRUE(count);
  ASSERT_TRUE(title);
  SaveWindowScreenshot(browser(), "kept-tab-waiting");
  ASSERT_TRUE(base::test::RunUntil([&] { return count->GetVisible(); }));
  ASSERT_TRUE(base::test::RunUntil([&] { return anchor->GetVisible(); }));
  EXPECT_LE(title->GetBoundsInScreen().right(), count->GetBoundsInScreen().x());
  EXPECT_LE(count->GetBoundsInScreen().right(),
            anchor->GetBoundsInScreen().x());
  SaveWindowScreenshot(browser(), "kept-tab");

  views::test::ButtonTestApi(views::Button::AsButton(anchor))
      .NotifyDefaultMouseClick();
  EXPECT_FALSE(controller()->IsKept(Id(page)));
  ASSERT_TRUE(base::test::RunUntil([&] { return !anchor->GetVisible(); }));
}

// The Scratchpad heading sits right above the first tab in no task. Clear
// shows at its end while the pointer is on it, its tooltip says how many tabs
// it closes, and a click closes them.
IN_PROC_BROWSER_TEST_F(TabOrganizationControllerBrowserTest,
                       TheScratchpadHeadingShowsClearUnderThePointer) {
  const auto instant = gfx::AnimationTestApi::SetRichAnimationRenderMode(
      gfx::Animation::RichAnimationRenderMode::FORCE_DISABLED);
  tabs::TabInterface* in_task = InitialTab();
  tabs::TabInterface* loose = NewTab();
  tabs::TabInterface* kept = NewTab();
  controller()->Execute(TabOrganizationController::kKeepHere, {Id(kept)});
  ASSERT_TRUE(controller()->MoveToNewTask({Id(in_task)}, u"Plan"));

  // Once the sidebar settles, the heading ends where the first tab in no
  // task starts.
  ScratchpadHeading* heading = nullptr;
  ASSERT_TRUE(base::test::RunUntil([&] {
    const std::vector<views::View*> headings =
        ViewsOfClass("ScratchpadHeading");
    heading = headings.size() == 1u
                  ? static_cast<ScratchpadHeading*>(headings[0])
                  : nullptr;
    if (!heading || !heading->GetVisible() ||
        heading->height() != kScratchpadHeadingHeight) {
      return false;
    }
    int first_loose = INT_MAX;
    for (views::View* row : TabRows()) {
      if (row->parent() == heading->parent()) {
        first_loose = std::min(first_loose, row->y());
      }
    }
    return first_loose == heading->bounds().bottom();
  }));
  views::LabelButton* clear = heading->clear_button();
  EXPECT_FALSE(clear->GetVisible());
  SaveWindowScreenshot(browser(), "scratchpad-heading");

  heading->OnMouseEntered(
      ui::MouseEvent(ui::EventType::kMouseEntered, gfx::PointF(), gfx::PointF(),
                     ui::EventTimeForNow(), ui::EF_NONE, ui::EF_NONE));
  EXPECT_TRUE(clear->GetVisible());
  EXPECT_TRUE(base::test::RunUntil([&] {
    return clear->GetTooltipText() == u"Close 1 tab";
  })) << clear->GetTooltipText();
  views::LabelButton* tidy = heading->tidy_button();
  EXPECT_TRUE(tidy->GetVisible());
  EXPECT_TRUE(tidy->GetEnabled());
  EXPECT_EQ(tidy->GetTooltipText(), u"Set up a language model to tidy");
  clear->SetState(views::Button::STATE_HOVERED);
  ViewsOfClass("ScratchpadHeading");
  EXPECT_LE(
      heading->GetBoundsInScreen().right() - clear->GetBoundsInScreen().right(),
      1);
  EXPECT_LE(tidy->GetBoundsInScreen().right(), clear->GetBoundsInScreen().x());
  SaveWindowScreenshot(browser(), "scratchpad-heading-clear");

  const PageId in_task_id = Id(in_task);
  const PageId kept_id = Id(kept);
  const PageId loose_id = Id(loose);
  views::test::ButtonTestApi(clear).NotifyDefaultMouseClick();
  EXPECT_TRUE(base::test::RunUntil([&] { return tabs()->count() == 2; }));
  EXPECT_TRUE(controller()->TabFor(in_task_id));
  EXPECT_TRUE(controller()->TabFor(kept_id));
  EXPECT_FALSE(controller()->TabFor(loose_id));
  ExpectStripMatchesTree();

  heading->OnMouseExited(
      ui::MouseEvent(ui::EventType::kMouseExited, gfx::PointF(), gfx::PointF(),
                     ui::EventTimeForNow(), ui::EF_NONE, ui::EF_NONE));
  EXPECT_FALSE(clear->GetVisible());
  EXPECT_FALSE(tidy->GetVisible());
}

// Clear Scratchpad in the Tasks menu closes the Scratchpad's tabs, a whole
// branch included, but not a kept tab or a task's tabs. Shift+Cmd+T reopens a
// cleared tab, and with only kept tabs left there is nothing to clear.
IN_PROC_BROWSER_TEST_F(TabOrganizationControllerBrowserTest,
                       ClearScratchpadClosesAllButKeptTabs) {
  ASSERT_TRUE(embedded_test_server()->Start());
  tabs::TabInterface* in_task = InitialTab();
  tabs::TabInterface* loose = NewTab();
  ASSERT_TRUE(content::NavigateToURL(
      loose->GetContents(), embedded_test_server()->GetURL("/title1.html")));
  OpenLinkFrom(loose, embedded_test_server()->GetURL("/title2.html"));
  tabs::TabInterface* kept = NewTab();
  controller()->Execute(TabOrganizationController::kKeepHere, {Id(kept)});
  ASSERT_TRUE(controller()->MoveToNewTask({Id(in_task)}, u"Plan"));
  const PageId in_task_id = Id(in_task);
  const PageId kept_id = Id(kept);
  ASSERT_EQ(tabs()->count(), 4);

  ASSERT_TRUE(CanRunWindowCommand(browser(), kClearScratchpad));
  chrome::ExecuteCommand(browser(), kClearScratchpad);
  ASSERT_TRUE(base::test::RunUntil([&] { return tabs()->count() == 2; }));
  EXPECT_TRUE(controller()->TabFor(in_task_id));
  EXPECT_TRUE(controller()->TabFor(kept_id));
  ExpectStripMatchesTree();
  EXPECT_FALSE(CanRunWindowCommand(browser(), kClearScratchpad));

  chrome::RestoreTab(browser());
  EXPECT_TRUE(base::test::RunUntil([&] { return tabs()->count() == 3; }));
}

// Clearing every tab of the window leaves a blank one, so the window stays.
IN_PROC_BROWSER_TEST_F(TabOrganizationControllerBrowserTest,
                       ClearingEveryTabLeavesABlankOne) {
  const PageId first = Id(InitialTab());
  const PageId second = Id(NewTab());
  controller()->ClearScratchpad();
  ASSERT_TRUE(base::test::RunUntil([&] {
    return tabs()->count() == 1 && !controller()->TabFor(first) &&
           !controller()->TabFor(second);
  }));
}

IN_PROC_BROWSER_TEST_F(TabOrganizationControllerBrowserTest,
                       TheTaskChevronTakesTheFolderIconsPlace) {
  const auto instant = gfx::AnimationTestApi::SetRichAnimationRenderMode(
      gfx::Animation::RichAnimationRenderMode::FORCE_DISABLED);
  ASSERT_TRUE(controller()->MoveToNewTask({Id(InitialTab())}, u"Plan"));
  std::vector<views::View*> headers = ViewsOfClass("TabGroupHeaderView");
  ASSERT_EQ(headers.size(), 1u);
  auto* header = static_cast<TabGroupHeaderView*>(headers[0]);
  views::View* folder = header->children().front();
  views::View* chevron = header->collapse_icon_for_testing();
  views::View* title = header->title_label_for_testing();
  EXPECT_TRUE(folder->GetVisible());
  EXPECT_FALSE(chevron->GetVisible());
  const gfx::Rect folder_bounds = folder->GetBoundsInScreen();
  const int title_x = title->GetBoundsInScreen().x();

  header->OnMouseEntered(
      ui::MouseEvent(ui::EventType::kMouseEntered, gfx::PointF(), gfx::PointF(),
                     ui::EventTimeForNow(), ui::EF_NONE, ui::EF_NONE));
  ViewsOfClass("TabGroupHeaderView");
  EXPECT_TRUE(chevron->GetVisible());
  EXPECT_FALSE(folder->GetVisible());
  EXPECT_EQ(chevron->GetBoundsInScreen(), folder_bounds);
  EXPECT_EQ(title->GetBoundsInScreen().x(), title_x);
  SaveWindowScreenshot(browser(), "task-chevron");

  header->OnMouseExited(
      ui::MouseEvent(ui::EventType::kMouseExited, gfx::PointF(), gfx::PointF(),
                     ui::EventTimeForNow(), ui::EF_NONE, ui::EF_NONE));
  ViewsOfClass("TabGroupHeaderView");
  EXPECT_FALSE(chevron->GetVisible());
  EXPECT_TRUE(folder->GetVisible());
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
    controller()->AddCommands(&menu, {Id(page)});
    return menu.GetLabelAt(
        menu.GetIndexOfCommandId(TabOrganizationController::kToggleBranch)
            .value());
  };
  EXPECT_EQ(label(), u"Collapse Branch");
  Collapse(page);
  EXPECT_EQ(label(), u"Expand Branch");
}

IN_PROC_BROWSER_TEST_F(TabOrganizationControllerBrowserTest,
                       NewChildTabSitsUnderNewTabBelow) {
  ui::SimpleMenuModel menu(nullptr);
  menu.AddItem(TabStripModel::CommandNewTabToRight, u"New tab below");
  menu.AddItem(TabStripModel::CommandReload, u"Reload");
  controller()->AddCommands(&menu, {Id(InitialTab())});
  EXPECT_EQ(menu.GetIndexOfCommandId(TabOrganizationController::kNewChildTab),
            1u);
  EXPECT_EQ(menu.GetLabelAt(1), u"New Child Tab");
}

// Chromium's tab menu gets uw's items beside the ones they go with: the moves
// after Move to task, the tree's commands after them, and Close branch under
// Close tab. Chromium's own new-group and remove-from-group items are gone.
IN_PROC_BROWSER_TEST_F(TabOrganizationControllerBrowserTest,
                       TheTabMenuKeepsMovesTreeCommandsAndClosesTogether) {
  tabs::TabInterface* plan = InitialTab();
  tabs::TabInterface* trip = NewTab();
  ASSERT_TRUE(controller()->MoveToNewTask({Id(plan)}, u"Plan"));
  ASSERT_TRUE(controller()->MoveToNewTask({Id(trip)}, u"Trip"));

  TabMenuModel menu(nullptr, browser()->GetFeatures().tab_menu_model_delegate(),
                    tabs(), tabs()->GetIndexOfTab(plan));
  controller()->AddCommands(&menu, {Id(plan)});
  std::vector<int> order;
  for (size_t i = 0; i < menu.GetItemCount(); ++i) {
    order.push_back(menu.GetCommandIdAt(i));
  }
  const auto at = [&](int command) {
    return std::ranges::find(order, command) - order.begin();
  };
  const auto has = [&](int command) {
    return std::ranges::find(order, command) != order.end();
  };
  const int window = has(TabStripModel::CommandMoveToExistingWindow)
                         ? TabStripModel::CommandMoveToExistingWindow
                         : TabStripModel::CommandMoveTabsToNewWindow;

  EXPECT_EQ(at(TabOrganizationController::kNewChildTab),
            at(TabStripModel::CommandNewTabToRight) + 1);
  EXPECT_EQ(at(TabOrganizationController::kNewTask),
            at(TabStripModel::CommandAddToExistingGroup) + 1);
  EXPECT_EQ(at(TabOrganizationController::kScratchpad),
            at(TabOrganizationController::kNewTask) + 1);
  EXPECT_EQ(at(window), at(TabOrganizationController::kScratchpad) + 1);
  EXPECT_LT(at(window), at(TabOrganizationController::kNest));
  EXPECT_EQ(at(TabOrganizationController::kOutdent),
            at(TabOrganizationController::kNest) + 1);
  EXPECT_EQ(at(TabOrganizationController::kToggleBranch),
            at(TabOrganizationController::kOutdent) + 1);
  EXPECT_EQ(at(TabOrganizationController::kUndo),
            at(TabOrganizationController::kToggleBranch) + 1);
  EXPECT_LT(at(TabOrganizationController::kUndo),
            at(TabStripModel::CommandReload));
  EXPECT_EQ(at(TabOrganizationController::kCloseBranch),
            at(TabStripModel::CommandCloseTab) + 1);
  EXPECT_FALSE(has(TabStripModel::CommandAddToNewGroup));
  EXPECT_FALSE(has(TabStripModel::CommandRemoveFromGroup));
}

// The right-clicked tab decides where the new tab goes, not the selected one.
IN_PROC_BROWSER_TEST_F(TabOrganizationControllerBrowserTest,
                       TheTabMenuOpensNewTabsByTheTabRightClicked) {
  tabs::TabInterface* parent = InitialTab();
  tabs::TabInterface* link = OpenLinkFrom(parent);
  tabs::TabInterface* other = NewTab();
  TabMenuDelegate delegate(tabs());
  TabContextMenuController menu(parent->GetHandle(), &delegate);

  ASSERT_TRUE(menu.IsCommandIdEnabled(TabOrganizationController::kNewChildTab));
  menu.ExecuteCommand(TabOrganizationController::kNewChildTab, 0);
  tabs::TabInterface* child = tabs()->GetActiveTab();
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(Strip(),
            (std::vector<PageId>{Id(parent), Id(child), Id(link), Id(other)}));
  EXPECT_EQ(controller()->tree().Find(Id(child))->parent, Id(parent));

  tabs()->ActivateTabAt(tabs()->GetIndexOfTab(other));
  menu.ExecuteCommand(TabStripModel::CommandNewTabToRight, 0);
  tabs::TabInterface* below = tabs()->GetActiveTab();
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(Strip(), (std::vector<PageId>{Id(parent), Id(child), Id(link),
                                          Id(below), Id(other)}));
  EXPECT_EQ(controller()->tree().Find(Id(below))->parent, PageId());
  ExpectStripMatchesTree();
}

IN_PROC_BROWSER_TEST_F(TabOrganizationControllerBrowserTest,
                       ANewChildTabExpandsACollapsedParent) {
  tabs::TabInterface* parent = InitialTab();
  OpenLinkFrom(parent);
  Collapse(parent);

  controller()->Execute(TabOrganizationController::kNewChildTab, {Id(parent)});

  EXPECT_FALSE(controller()->IsCollapsed(Id(parent)));
  EXPECT_FALSE(controller()->IsHidden(Id(tabs()->GetActiveTab())));
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

constexpr char kRestoredTree[] = "e0@Plan/you f1@Plan/you a0 b1* c2 d1";

// Chromium rewrites its session file now and then, for example every 250
// changes. The rewrite used to leave out the tree's data.
IN_PROC_BROWSER_TEST_F(TabOrganizationControllerBrowserTest,
                       PRE_TheTreeComesBackAfterTheSessionFileIsRewritten) {
  BuildTreeToRestore();
  ASSERT_EQ(TreeShape(), kRestoredTree);
  SessionServiceFactory::GetForProfile(browser()->GetProfile())
      ->ResetFromCurrentBrowsers();
  base::RunLoop().RunUntilIdle();
}

IN_PROC_BROWSER_TEST_F(TabOrganizationControllerBrowserTest,
                       TheTreeComesBackAfterTheSessionFileIsRewritten) {
  EXPECT_TRUE(base::test::RunUntil([&] { return TreeShape() == kRestoredTree; }))
      << TreeShape();
}

// After a crash, Chromium saves nothing until its restore has run, and then
// rewrites the file. The tree built meanwhile must be in it.
IN_PROC_BROWSER_TEST_F(TabOrganizationControllerBrowserTest,
                       PRE_TheTreeComesBackWhenItWasBuiltWhileSavingWasOff) {
  SessionServiceTestHelper session(browser()->GetProfile());
  session.SetSavingEnabled(false);
  BuildTreeToRestore();
  ASSERT_EQ(TreeShape(), kRestoredTree);
  session.SetSavingEnabled(true);
  base::RunLoop().RunUntilIdle();
}

IN_PROC_BROWSER_TEST_F(TabOrganizationControllerBrowserTest,
                       TheTreeComesBackWhenItWasBuiltWhileSavingWasOff) {
  EXPECT_TRUE(base::test::RunUntil([&] { return TreeShape() == kRestoredTree; }))
      << TreeShape();
}

// On the Mac, uw keeps running when its last window closes, and the next new
// window brings back the one that closed.
IN_PROC_BROWSER_TEST_F(TabOrganizationControllerBrowserTest,
                       TheTreeComesBackInANewWindowAfterTheLastOneCloses) {
  BuildTreeToRestore();
  ASSERT_EQ(TreeShape(), kRestoredTree);
  SessionServiceFactory::GetForProfile(browser()->GetProfile())
      ->ResetFromCurrentBrowsers();
  base::RunLoop().RunUntilIdle();

  Profile* profile = browser()->GetProfile();
  ScopedKeepAlive keep_alive(KeepAliveOrigin::SESSION_RESTORE,
                             KeepAliveRestartOption::DISABLED);
  ScopedProfileKeepAlive profile_keep_alive(
      profile, ProfileKeepAliveOrigin::kBrowserWindow);
  CloseBrowserSynchronously(browser());
  SessionRestoreTestHelper restore;
  SessionServiceTestHelper(profile).SetForceBrowserNotAliveWithNoWindows(true);
  chrome::NewEmptyWindow(profile);
  restore.Wait();
  ASSERT_EQ(GlobalBrowserCollection::GetInstance()->GetSize(), 1u);
  SetBrowser(GlobalBrowserCollection::GetInstance()->GetLastActiveBrowser());
  EXPECT_TRUE(base::test::RunUntil([&] { return TreeShape() == kRestoredTree; }))
      << TreeShape();
}

// A launch restores the last session, though the user set nothing.
IN_PROC_BROWSER_TEST_F(TabOrganizationControllerBrowserTest,
                       PRE_ALaunchRestoresTheLastSession) {
  OpenAWithBUnderIt();
}

IN_PROC_BROWSER_TEST_F(TabOrganizationControllerBrowserTest,
                       ALaunchRestoresTheLastSession) {
  EXPECT_EQ(SessionStartupPref::GetStartupPref(browser()->GetProfile()).type,
            SessionStartupPref::LAST);
  EXPECT_TRUE(base::test::RunUntil([&] { return TreeShape() == "a0 b1"; }))
      << TreeShape();
}

// After a crash Chromium asks before it restores. Its prompt has a box,
// unticked, that makes every later crash restore without asking.
IN_PROC_BROWSER_TEST_F(TabOrganizationControllerBrowserTest,
                       TickingAlwaysRestoreInTheCrashPromptKeepsIt) {
  PrefService* prefs = browser()->GetProfile()->GetPrefs();
  ASSERT_FALSE(prefs->GetBoolean(kRestoreAfterCrashPref));
  SessionCrashedBubble::ShowIfNotOffTheRecordProfile(browser(), true);
  views::BubbleDialogDelegate* prompt =
      SessionCrashedBubbleView::GetInstanceForTest();
  ASSERT_TRUE(prompt);
  // The prompt draws a checkbox's words as a label beside the box. This
  // build offers no other checkbox there.
  std::vector<views::Checkbox*> boxes;
  bool labelled = false;
  const std::u16string label =
      l10n_util::GetStringUTF16(IDS_UW_SESSION_CRASHED_ALWAYS_RESTORE);
  const std::function<void(views::View*)> find = [&](views::View* view) {
    if (auto* box = views::AsViewClass<views::Checkbox>(view)) {
      boxes.push_back(box);
    }
    if (auto* text = views::AsViewClass<views::StyledLabel>(view)) {
      labelled |= text->GetText() == label;
    }
    if (auto* text = views::AsViewClass<views::Label>(view)) {
      labelled |= text->GetText() == label;
    }
    for (views::View* child : view->children()) {
      find(child);
    }
  };
  find(prompt->GetWidget()->GetRootView());
  ASSERT_EQ(boxes.size(), 1u);
  EXPECT_TRUE(labelled);
  views::Checkbox* always = boxes[0];
  EXPECT_FALSE(always->GetChecked());

  views::test::ButtonTestApi(always).NotifyClick(
      ui::MouseEvent(ui::EventType::kMousePressed, gfx::Point(), gfx::Point(),
                     ui::EventTimeForNow(), ui::EF_LEFT_MOUSE_BUTTON, 0));
  prompt->AcceptDialog();
  EXPECT_TRUE(prefs->GetBoolean(kRestoreAfterCrashPref));
}

IN_PROC_BROWSER_TEST_F(TabOrganizationControllerBrowserTest,
                       PRE_OnceTickedACrashRestoresWithoutTheCrashPrompt) {
  // Starting blank keeps this launch's own restore out of the way.
  SessionStartupPref::SetStartupPref(
      browser()->GetProfile(), SessionStartupPref(SessionStartupPref::DEFAULT));
  OpenAWithBUnderIt();
}

IN_PROC_BROWSER_TEST_F(TabOrganizationControllerBrowserTest,
                       OnceTickedACrashRestoresWithoutTheCrashPrompt) {
  ASSERT_EQ(TreeShape(), "");
  browser()->GetProfile()->GetPrefs()->SetBoolean(kRestoreAfterCrashPref,
                                                  true);
  SessionCrashedBubble::ShowIfNotOffTheRecordProfile(browser(), true);
  EXPECT_FALSE(SessionCrashedBubbleView::GetInstanceForTest());
  EXPECT_TRUE(base::test::RunUntil([&] { return TreeShape() == "a0 b1"; }))
      << TreeShape();
}

IN_PROC_BROWSER_TEST_F(TabOrganizationControllerBrowserTest,
                       WindowCloseWithTasksTearsDownCleanly) {
  tabs::TabInterface* page = InitialTab();
  OpenLinkFrom(page);
  ASSERT_TRUE(controller()->MoveToNewTask({Id(page)}, u"Keep"));
  CloseAllBrowsers();
}

// Waits until the tab group sync service has loaded its saved groups.
void WaitUntilSavedGroupsLoad(tab_groups::TabGroupSyncService* service) {
  class Loaded : public tab_groups::TabGroupSyncService::Observer {
   public:
    void OnInitialized() override { done = true; }
    bool done = false;
  } loaded;
  service->AddObserver(&loaded);
  EXPECT_TRUE(base::test::RunUntil([&] { return loaded.done; }));
  service->RemoveObserver(&loaded);
  base::RunLoop().RunUntilIdle();
}

bool KeepsAClosedTask(const tab_groups::TabGroupSyncService* service) {
  return std::ranges::any_of(service->GetAllGroups(), [](const auto& group) {
    return !group.local_group_id().has_value();
  });
}

IN_PROC_BROWSER_TEST_F(TabOrganizationControllerBrowserTest,
                       PRE_AClosedTaskIsClearedOnTheNextStart) {
  tab_groups::TabGroupSyncService* service =
      tab_groups::TabGroupSyncServiceFactory::GetForProfile(
          browser()->GetProfile());
  ASSERT_TRUE(service);
  WaitUntilSavedGroupsLoad(service);
  const base::Uuid saved = base::Uuid::GenerateRandomV4();
  service->AddGroup(tab_groups::SavedTabGroup(
      u"Plan", tab_groups::TabGroupColorId::kBlue,
      {tab_groups::SavedTabGroupTab(GURL("https://example.com/"), u"Example",
                                    saved, 0)},
      std::nullopt, saved));
  ASSERT_TRUE(base::test::RunUntil([&] { return KeepsAClosedTask(service); }));
  base::RunLoop().RunUntilIdle();
}

// Chromium keeps a closed task as a saved group. uw clears those when it
// starts, so none is ever offered again.
IN_PROC_BROWSER_TEST_F(TabOrganizationControllerBrowserTest,
                       AClosedTaskIsClearedOnTheNextStart) {
  tab_groups::TabGroupSyncService* service =
      tab_groups::TabGroupSyncServiceFactory::GetForProfile(
          browser()->GetProfile());
  ASSERT_TRUE(service);
  WaitUntilSavedGroupsLoad(service);
  EXPECT_FALSE(KeepsAClosedTask(service));
}

// The empty space under the last tab drags the window. Once tabs fill the
// sidebar, the layout hides that space and leaves its old bounds over the
// tabs, which must still take clicks rather than drag the window.
IN_PROC_BROWSER_TEST_F(TabOrganizationControllerBrowserTest,
                       TabsThatFillTheSidebarTakeClicks) {
  views::View* empty_space = ViewsOfClass("VerticalTabStripBottomContainer")[0];
  ASSERT_TRUE(empty_space->GetVisible());
  const gfx::Rect old_bounds = empty_space->GetBoundsInScreen();
  for (int i = 0; i < 40 && empty_space->GetVisible(); ++i) {
    NewTab();
    ViewsOfClass("VerticalTabStripBottomContainer");
  }
  ASSERT_FALSE(empty_space->GetVisible());

  int checked = 0;
  for (views::View* row : VisibleRows()) {
    if (!old_bounds.Contains(row->GetBoundsInScreen().CenterPoint())) {
      continue;
    }
    views::Widget* widget = row->GetWidget();
    gfx::Point point = row->GetLocalBounds().CenterPoint();
    views::View::ConvertPointToWidget(row, &point);
    if (widget->GetRootView()->GetEventHandlerForPoint(point) != row) {
      continue;
    }
    EXPECT_EQ(widget->GetNonClientComponent(point), HTCLIENT);
    ++checked;
  }
  EXPECT_GT(checked, 0);
}
}  // namespace
}  // namespace uw
