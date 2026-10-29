// Copyright 2026 The uw Authors. All rights reserved.

#include <optional>
#include <string>
#include <vector>

#include "base/functional/bind.h"
#include "base/run_loop.h"
#include "base/strings/stringprintf.h"
#include "base/strings/utf_string_conversions.h"
#include "base/test/run_until.h"
#include "base/test/scoped_feature_list.h"
#include "chrome/browser/headless/test/headless_mode_browsertest.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/tabs/tab_enums.h"
#include "chrome/browser/ui/tabs/tab_group_model.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/tabs/tab_strip_user_gesture_details.h"
#include "components/tab_groups/tab_group_color.h"
#include "components/tab_groups/tab_group_id.h"
#include "components/tab_groups/tab_group_visual_data.h"
#include "components/tabs/public/tab_group.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "ui/base/window_open_disposition.h"
#include "url/gurl.h"
#include "uw/browser/tab_organization/shared_tree.h"
#include "uw/browser/tab_organization/tab_organization_controller.h"
#include "uw/browser/ui/views/pins/inactive_pin_overlay.h"
#include "uw/browser/workspaces/workspace_list.h"
#include "uw/browser/workspaces/workspace_window.h"

namespace uw {
namespace {

constexpr char kMail[] = "data:text/html,<title>Mail</title>";
constexpr char kIssues[] = "data:text/html,<title>Issues</title>";
constexpr char kIssue[] = "data:text/html,<title>Issue 412</title>";
constexpr char kNotes[] = "data:text/html,<title>Notes</title><input id=f>";
constexpr char kTypeIntoNotes[] =
    "document.getElementById('f').value = 'half-typed'";
constexpr char kNotesInput[] = "document.getElementById('f').value";

TabOrganizationController* ControllerOf(Browser* window) {
  return TabOrganizationController::From(window);
}

// Each regular tab of `window`, in order: its address, its task's title, or
// "-" for the Scratchpad, with "folded" when the task is, and its depth.
std::vector<std::string> Layout(Browser* window) {
  TabStripModel* tabs = window->tab_strip_model();
  std::vector<std::string> rows;
  for (int i = tabs->IndexOfFirstNonPinnedTab(); i < tabs->count(); ++i) {
    tabs::TabInterface* tab = tabs->GetTabAtIndex(i);
    std::string task = "-";
    if (const std::optional<tab_groups::TabGroupId> group = tab->GetGroup()) {
      const tab_groups::TabGroupVisualData* visuals =
          tabs->group_model()->GetTabGroup(*group)->visual_data();
      task = base::UTF16ToUTF8(visuals->title()) +
             (visuals->is_collapsed() ? " folded" : "");
    }
    rows.push_back(base::StringPrintf(
        "%s %s %d", tab->GetContents()->GetLastCommittedURL().spec().c_str(),
        task.c_str(), ControllerOf(window)->DepthFor(tab)));
  }
  return rows;
}

// `window`'s tab for the page at `url`.
tabs::TabInterface* TabAt(Browser* window, const char* url) {
  TabStripModel* tabs = window->tab_strip_model();
  for (int i = 0; i < tabs->count(); ++i) {
    if (tabs->GetWebContentsAt(i)->GetLastCommittedURL() == GURL(url)) {
      return tabs->GetTabAtIndex(i);
    }
  }
  return nullptr;
}

bool InStep(Browser* one, Browser* two) {
  return Layout(one) == Layout(two);
}

void SelectByClick(Browser* window, tabs::TabInterface* tab) {
  window->tab_strip_model()->ActivateTabAt(
      window->tab_strip_model()->GetIndexOfTab(tab),
      TabStripUserGestureDetails(
          TabStripUserGestureDetails::GestureType::kMouse));
}

class SharedTabsBrowserTest : public headless::HeadlessModeBrowserTest {
 public:
  SharedTabsBrowserTest() { features_.InitAndEnableFeature(kUwSharedTabs); }

 protected:
  tabs::TabInterface* Open(Browser* window,
                           const char* url,
                           bool select = false) {
    content::WebContents* page =
        chrome::AddAndReturnTabAt(window, GURL(url), -1, select);
    EXPECT_TRUE(content::WaitForLoadStop(page));
    base::RunLoop().RunUntilIdle();
    return window->tab_strip_model()->GetTabForWebContents(page);
  }

 private:
  base::test::ScopedFeatureList features_;
};

IN_PROC_BROWSER_TEST_F(SharedTabsBrowserTest, WindowsOnAWorkspaceShowOneTree) {
  Browser* first = browser();
  Open(first, kMail);
  Open(first, kIssues);
  Browser* second = CreateBrowser(first->GetProfile());
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return Layout(second).size() == 4u && InStep(first, second); }));

  // A tab opened in either window shows in the other.
  Open(second, kIssue);
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return Layout(first).size() == 5u && InStep(first, second); }));

  // A task made in one window, renamed in the other, and folded in the first.
  const std::optional<PageId> mail =
      ControllerOf(first)->IdFor(TabAt(first, kMail));
  const std::optional<PageId> issues =
      ControllerOf(first)->IdFor(TabAt(first, kIssues));
  ASSERT_TRUE(mail && issues);
  ASSERT_TRUE(ControllerOf(first)->MoveToNewTask({*mail, *issues}, u"Trip"));
  ASSERT_TRUE(base::test::RunUntil([&] {
    return InStep(first, second) && TabAt(second, kMail)->GetGroup();
  }));
  EXPECT_NE(Layout(second)[0].find(" Trip "), std::string::npos);

  TabStripModel* second_tabs = second->tab_strip_model();
  const tab_groups::TabGroupId copy = *TabAt(second, kMail)->GetGroup();
  second_tabs->ChangeTabGroupVisuals(
      copy, tab_groups::TabGroupVisualData(
                u"Lisbon", tab_groups::TabGroupColorId::kGreen));
  ASSERT_TRUE(base::test::RunUntil([&] { return InStep(first, second); }));
  EXPECT_NE(Layout(first)[0].find(" Lisbon "), std::string::npos);

  const tab_groups::TabGroupId task = *TabAt(first, kMail)->GetGroup();
  first->tab_strip_model()->ChangeTabGroupVisuals(
      task, tab_groups::TabGroupVisualData(u"Lisbon",
                                           tab_groups::TabGroupColorId::kGreen,
                                           /*is_collapsed=*/true));
  ASSERT_TRUE(base::test::RunUntil([&] { return InStep(first, second); }));
  EXPECT_NE(Layout(second)[0].find(" Lisbon folded "), std::string::npos);

  // Nesting in one window indents the other. A tab opened in the background
  // goes right under the selected tab, so Issues is above Mail.
  ASSERT_TRUE(ControllerOf(first)->Nest(*mail));
  ASSERT_TRUE(base::test::RunUntil([&] {
    return InStep(first, second) &&
           ControllerOf(second)->DepthFor(TabAt(second, kMail)) == 1;
  }));

  // Closing a copy closes the page in every window, and its child moves up.
  second_tabs->CloseWebContentsAt(
      second_tabs->GetIndexOfTab(TabAt(second, kIssues)),
      TabCloseTypes::CLOSE_USER_GESTURE);
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return !TabAt(first, kIssues) && InStep(first, second); }));
  EXPECT_EQ(ControllerOf(first)->DepthFor(TabAt(first, kMail)), 0);
}

IN_PROC_BROWSER_TEST_F(SharedTabsBrowserTest, AnotherWorkspaceShowsNoneOfIt) {
  Browser* first = browser();
  Open(first, kMail);
  WorkspaceList& list = WorkspaceList::Get();
  const std::string personal =
      list.Add("Personal", std::string(list.UnusedIcon()),
               first->GetProfile()->GetBaseName());
  BrowserWindowInterface* partner = nullptr;
  WorkspaceWindow::From(first)->WithPartner(
      personal,
      base::BindOnce([](BrowserWindowInterface** partner,
                        BrowserWindowInterface* opened) { *partner = opened; },
                     &partner));
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return partner && partner->GetTabStripModel()->count() == 1; }));
  base::RunLoop().RunUntilIdle();
  EXPECT_FALSE(TabAt(partner->GetBrowserForMigrationOnly(), kMail));
  EXPECT_EQ(Layout(first).size(), 2u);
}

IN_PROC_BROWSER_TEST_F(SharedTabsBrowserTest, SelectingACopyBringsItsLivePage) {
  Browser* first = browser();
  tabs::TabInterface* notes = Open(first, kNotes, /*select=*/true);
  ASSERT_TRUE(content::ExecJs(notes->GetContents(), kTypeIntoNotes));
  Browser* second = CreateBrowser(first->GetProfile());
  ASSERT_TRUE(base::test::RunUntil([&] { return InStep(first, second); }));
  tabs::TabInterface* copy = TabAt(second, kNotes);
  ASSERT_TRUE(TabOrganizationController::IsCopy(copy));

  SelectByClick(second, copy);
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return !TabOrganizationController::IsCopy(copy); }));
  EXPECT_TRUE(TabOrganizationController::IsCopy(notes));
  EXPECT_EQ("half-typed", content::EvalJs(copy->GetContents(), kNotesInput));
  // The first window still has it selected, so it shows the hazed view.
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return InactivePinOverlay::From(first)->GetShownView(); }));

  ControllerOf(first)->BringLivePage(notes);
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return !TabOrganizationController::IsCopy(notes); }));
  EXPECT_EQ("half-typed", content::EvalJs(notes->GetContents(), kNotesInput));
  EXPECT_FALSE(InactivePinOverlay::From(first)->GetShownView());
}

IN_PROC_BROWSER_TEST_F(SharedTabsBrowserTest, ACopyFollowsItsLivePage) {
  Browser* first = browser();
  tabs::TabInterface* notes = Open(first, kNotes);
  Browser* second = CreateBrowser(first->GetProfile());
  ASSERT_TRUE(base::test::RunUntil([&] { return InStep(first, second); }));
  ASSERT_TRUE(
      content::ExecJs(notes->GetContents(), "document.title = 'Plans'"));
  ASSERT_TRUE(base::test::RunUntil([&] {
    return TabAt(second, kNotes)->GetContents()->GetTitle() == u"Plans";
  }));
}

IN_PROC_BROWSER_TEST_F(SharedTabsBrowserTest, ReloadInACopyRunsInTheLivePage) {
  Browser* first = browser();
  tabs::TabInterface* notes = Open(first, kNotes, /*select=*/true);
  Browser* second = CreateBrowser(first->GetProfile());
  ASSERT_TRUE(base::test::RunUntil([&] { return InStep(first, second); }));
  tabs::TabInterface* copy = TabAt(second, kNotes);
  // Selected without a click, while the first window shows it, so it stays a
  // copy.
  second->tab_strip_model()->ActivateTabAt(
      second->tab_strip_model()->GetIndexOfTab(copy));
  base::RunLoop().RunUntilIdle();
  ASSERT_TRUE(TabOrganizationController::IsCopy(copy));

  chrome::Reload(second, WindowOpenDisposition::CURRENT_TAB);
  ASSERT_TRUE(base::test::RunUntil([&] {
    return !TabOrganizationController::IsCopy(copy) &&
           TabOrganizationController::IsCopy(notes);
  }));
}

IN_PROC_BROWSER_TEST_F(SharedTabsBrowserTest, PrivateWindowsKeepTheirOwnTabs) {
  Browser* first = browser();
  Open(first, kMail);
  Browser* private_window = CreateIncognitoBrowser(first->GetProfile());
  Open(private_window, kIssues);
  base::RunLoop().RunUntilIdle();
  EXPECT_FALSE(TabAt(private_window, kMail));
  EXPECT_FALSE(TabAt(first, kIssues));
}

class SharedTabsOffBrowserTest : public headless::HeadlessModeBrowserTest {};

IN_PROC_BROWSER_TEST_F(SharedTabsOffBrowserTest, EachWindowKeepsItsOwnTabs) {
  Browser* first = browser();
  content::WebContents* mail =
      chrome::AddAndReturnTabAt(first, GURL(kMail), -1, false);
  ASSERT_TRUE(content::WaitForLoadStop(mail));
  Browser* second = CreateBrowser(first->GetProfile());
  base::RunLoop().RunUntilIdle();
  EXPECT_FALSE(TabAt(second, kMail));
  EXPECT_EQ(Layout(second).size(), 1u);
}

}  // namespace
}  // namespace uw
