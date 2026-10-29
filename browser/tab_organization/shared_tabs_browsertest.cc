// Copyright 2026 The uw Authors. All rights reserved.

#include <algorithm>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "base/functional/bind.h"
#include "base/run_loop.h"
#include "base/strings/stringprintf.h"
#include "base/strings/utf_string_conversions.h"
#include "base/test/run_until.h"
#include "base/test/scoped_feature_list.h"
#include "chrome/app/chrome_command_ids.h"
#include "chrome/browser/autocomplete/tab_matcher_desktop.h"
#include "chrome/browser/headless/test/headless_mode_browsertest.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/sessions/tab_restore_service_factory.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface_iterator.h"
#include "chrome/browser/ui/tabs/tab_enums.h"
#include "chrome/browser/ui/tabs/tab_group_model.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/tabs/tab_strip_user_gesture_details.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/tabs/common/tab_view.h"
#include "components/performance_manager/public/graph/page_node.h"
#include "components/performance_manager/public/performance_manager.h"
#include "components/sessions/core/tab_restore_service.h"
#include "components/sessions/core/tab_restore_types.h"
#include "components/tab_groups/tab_group_color.h"
#include "components/tab_groups/tab_group_id.h"
#include "components/tab_groups/tab_group_visual_data.h"
#include "components/tabs/public/tab_group.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "net/test/embedded_test_server/http_request.h"
#include "net/test/embedded_test_server/http_response.h"
#include "ui/base/base_window.h"
#include "ui/base/window_open_disposition.h"
#include "ui/views/view.h"
#include "ui/views/view_utils.h"
#include "url/gurl.h"
#include "uw/browser/tab_organization/shared_tree.h"
#include "uw/browser/tab_organization/tab_organization_controller.h"
#include "uw/browser/tab_organization/window_screenshot.h"
#include "uw/browser/ui/views/command_bar/command_bar.h"
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

views::View* FindRow(views::View* view, const tabs::TabInterface* tab) {
  if (auto* row = views::AsViewClass<TabView>(view);
      row && row->GetTabInterface() == tab) {
    return row;
  }
  for (views::View* child : view->children()) {
    if (views::View* row = FindRow(child, tab)) {
      return row;
    }
  }
  return nullptr;
}

// The row in `window`'s tab strip that shows `tab`.
views::View* RowOf(Browser* window, const tabs::TabInterface* tab) {
  return FindRow(BrowserView::GetBrowserViewForBrowser(window), tab);
}

bool IsOpen(BrowserWindowInterface* window) {
  return std::ranges::contains(GetAllBrowserWindowInterfaces(), window);
}

// The window opened since `known` were, or null.
Browser* NewWindow(const std::vector<BrowserWindowInterface*>& known) {
  for (BrowserWindowInterface* window : GetAllBrowserWindowInterfaces()) {
    if (!std::ranges::contains(known, window)) {
      return window->GetBrowserForMigrationOnly();
    }
  }
  return nullptr;
}

// How many of `window`'s tabs show the new tab page.
int BlankTabs(Browser* window) {
  int blank = 0;
  for (int i = 0; i < window->tab_strip_model()->count(); ++i) {
    const GURL& url =
        window->tab_strip_model()->GetWebContentsAt(i)->GetVisibleURL();
    if (url.SchemeIs("chrome") &&
        (url.host() == "newtab" || url.host() == "new-tab-page")) {
      ++blank;
    }
  }
  return blank;
}

class SharedTabsBrowserTest : public headless::HeadlessModeBrowserTest {
 public:
  SharedTabsBrowserTest() { features_.InitAndEnableFeature(kUwSharedTabs); }

  void SetUpOnMainThread() override {
    headless::HeadlessModeBrowserTest::SetUpOnMainThread();
    // A page that counts in its title, as a timer does.
    embedded_test_server()->RegisterRequestHandler(base::BindRepeating(
        [](const net::test_server::HttpRequest& request)
            -> std::unique_ptr<net::test_server::HttpResponse> {
          auto response =
              std::make_unique<net::test_server::BasicHttpResponse>();
          response->set_content_type("text/html");
          response->set_content(
              request.relative_url == "/timer"
                  ? "<title>0</title><script>let n = 0;"
                    "setInterval(() => document.title = ++n, 1000)</script>"
                  : "<title>Plain</title>");
          return response;
        }));
    ASSERT_TRUE(embedded_test_server()->Start());
  }

 protected:
  // Waits until `page` has loaded and gone quiet, from when Memory Saver
  // counts its title changes out of sight as work, and for one more change.
  static void WaitUntilAtWork(content::WebContents* page) {
    ASSERT_TRUE(base::test::RunUntil([&] {
      base::WeakPtr<performance_manager::PageNode> node = performance_manager::
          PerformanceManager::GetPrimaryPageNodeForWebContents(page);
      return node &&
             node->GetLoadingState() ==
                 performance_manager::PageNode::LoadingState::kLoadedIdle;
    }));
    const std::u16string title = page->GetTitle();
    ASSERT_TRUE(
        base::test::RunUntil([&] { return page->GetTitle() != title; }));
  }

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

IN_PROC_BROWSER_TEST_F(SharedTabsBrowserTest,
                       ClosingOneOfTwoWindowsClosesNoTab) {
  Browser* first = browser();
  Profile* profile = first->GetProfile();
  tabs::TabInterface* notes = Open(first, kNotes, /*select=*/true);
  ASSERT_TRUE(content::ExecJs(notes->GetContents(), kTypeIntoNotes));
  Browser* second = CreateBrowser(profile);
  ASSERT_TRUE(base::test::RunUntil([&] { return InStep(first, second); }));

  CloseBrowserSynchronously(first);
  tabs::TabInterface* kept = TabAt(second, kNotes);
  ASSERT_TRUE(kept);
  // The second window took the live page, half-typed input and all.
  EXPECT_FALSE(TabOrganizationController::IsCopy(kept));
  EXPECT_EQ("half-typed", content::EvalJs(kept->GetContents(), kNotesInput));
  // Cmd+Shift+T has no window to bring back.
  for (const auto& entry :
       TabRestoreServiceFactory::GetForProfile(profile)->entries()) {
    EXPECT_NE(entry->type, sessions::tab_restore::WINDOW);
  }
}

IN_PROC_BROWSER_TEST_F(SharedTabsBrowserTest,
                       TheLastWindowKeepsItsTabsUntilAnotherShowsThem) {
  Profile* profile = browser()->GetProfile();
  // A private window, so the app keeps a window of its own.
  CreateIncognitoBrowser(profile);
  Browser* first = browser();
  Open(first, kMail);
  content::WebContents* timer = chrome::AddAndReturnTabAt(
      first, embedded_test_server()->GetURL("/timer"), -1, false);
  WaitUntilAtWork(timer);
  const GURL timer_url = timer->GetLastCommittedURL();

  // Closing it hides it. The page doing nothing unloads, and the timer
  // keeps counting.
  first->GetWindow()->Close();
  WorkspaceWindow* kept = WorkspaceWindow::From(first);
  ASSERT_TRUE(base::test::RunUntil([&] { return kept->keeper(); }));
  EXPECT_TRUE(IsOpen(first));
  EXPECT_FALSE(first->GetWindow()->IsVisible());
  EXPECT_TRUE(TabOrganizationController::IsCopy(TabAt(first, kMail)));
  EXPECT_FALSE(TabOrganizationController::IsCopy(
      first->tab_strip_model()->GetTabForWebContents(timer)));

  // A new window on the workspace shows the tabs, takes the timer live, and
  // the kept window closes.
  Browser* second = CreateBrowser(profile);
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return !IsOpen(first) && TabAt(second, kMail); }));
  tabs::TabInterface* counting = nullptr;
  for (int i = 0; i < second->tab_strip_model()->count(); ++i) {
    if (second->tab_strip_model()->GetWebContentsAt(i)->GetLastCommittedURL() ==
        timer_url) {
      counting = second->tab_strip_model()->GetTabAtIndex(i);
    }
  }
  ASSERT_TRUE(counting);
  EXPECT_FALSE(TabOrganizationController::IsCopy(counting));
  // The unloaded page loads again once selected.
  tabs::TabInterface* mail = TabAt(second, kMail);
  SelectByClick(second, mail);
  ASSERT_TRUE(base::test::RunUntil([&] {
    return !TabOrganizationController::IsCopy(mail) &&
           mail->GetContents()->GetTitle() == u"Mail";
  }));
}

IN_PROC_BROWSER_TEST_F(SharedTabsBrowserTest,
                       TheLastTabClosesEveryWindowOnTheWorkspace) {
  Profile* profile = browser()->GetProfile();
  CreateIncognitoBrowser(profile);
  Browser* first = browser();
  // A pin keeps each window's tab strip from emptying.
  tabs::TabInterface* pin = Open(first, kMail);
  first->tab_strip_model()->SetTabPinned(
      first->tab_strip_model()->GetIndexOfTab(pin), true);
  Browser* second = CreateBrowser(profile);
  ASSERT_TRUE(base::test::RunUntil([&] {
    return InStep(first, second) && TabAt(second, kMail) &&
           second->tab_strip_model()->IndexOfFirstNonPinnedTab() == 1;
  }));

  // Closing the last tab closes the windows, so they close in one go.
  TabStripModel* tabs = first->tab_strip_model();
  const int regular = tabs->count() - tabs->IndexOfFirstNonPinnedTab();
  for (int i = 0; i < regular; ++i) {
    tabs->CloseWebContentsAt(tabs->count() - 1,
                             TabCloseTypes::CLOSE_USER_GESTURE);
  }
  EXPECT_TRUE(
      base::test::RunUntil([&] { return !IsOpen(first) && !IsOpen(second); }));
}

// Moving a tab to a window of its own, as dragging it out does, opens a
// window on its workspace that shows it, and the tab keeps its place. Moving
// it back closes that window.
IN_PROC_BROWSER_TEST_F(SharedTabsBrowserTest,
                       ATabMovedOutKeepsItsPlaceAndItsWindowClosesAfter) {
  Browser* first = browser();
  Open(first, kMail);
  tabs::TabInterface* issues = Open(first, kIssues);
  Open(first, kIssue);
  const std::vector<std::string> before = Layout(first);
  const PageId id = ControllerOf(first)->IdFor(issues).value();
  const std::vector<BrowserWindowInterface*> known =
      GetAllBrowserWindowInterfaces();

  chrome::MoveTabsToNewWindow(
      first, {first->tab_strip_model()->GetIndexOfTab(issues)});
  Browser* own = nullptr;
  ASSERT_TRUE(base::test::RunUntil([&] {
    own = NewWindow(known);
    return own && ControllerOf(own)->TabFor(id) && InStep(first, own);
  }));
  EXPECT_EQ(Layout(first), before);
  EXPECT_EQ(own->tab_strip_model()->GetActiveTab(),
            ControllerOf(own)->TabFor(id));
  EXPECT_FALSE(TabOrganizationController::IsCopy(TabAt(own, kIssues)));
  EXPECT_TRUE(TabOrganizationController::IsCopy(TabAt(first, kIssues)));

  chrome::MoveTabsToExistingWindow(
      own, first, {own->tab_strip_model()->GetIndexOfTab(TabAt(own, kIssues))});
  ASSERT_TRUE(base::test::RunUntil([&] {
    return !IsOpen(own) && TabAt(first, kIssues) &&
           !TabOrganizationController::IsCopy(TabAt(first, kIssues));
  }));
  EXPECT_EQ(std::ranges::count_if(Layout(first),
                                  [](const std::string& row) {
                                    return row.starts_with(
                                        std::string(kIssues) + " ");
                                  }),
            1);
}

IN_PROC_BROWSER_TEST_F(SharedTabsBrowserTest, AWindowUsedForAnotherTabStays) {
  Browser* first = browser();
  Open(first, kMail);
  tabs::TabInterface* issues = Open(first, kIssues);
  const std::vector<BrowserWindowInterface*> known =
      GetAllBrowserWindowInterfaces();
  chrome::MoveTabsToNewWindow(
      first, {first->tab_strip_model()->GetIndexOfTab(issues)});
  Browser* own = nullptr;
  ASSERT_TRUE(base::test::RunUntil([&] {
    own = NewWindow(known);
    return own && TabAt(own, kMail) && InStep(first, own);
  }));

  SelectByClick(own, TabAt(own, kMail));
  chrome::MoveTabsToExistingWindow(
      own, first, {own->tab_strip_model()->GetIndexOfTab(TabAt(own, kIssues))});
  ASSERT_TRUE(base::test::RunUntil([&] {
    return !TabOrganizationController::IsCopy(TabAt(first, kIssues));
  }));
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(IsOpen(own));
}

IN_PROC_BROWSER_TEST_F(SharedTabsBrowserTest,
                       ATabMovedToAnotherWorkspaceLeavesThisOne) {
  Browser* first = browser();
  Profile* profile = first->GetProfile();
  tabs::TabInterface* mail = Open(first, kMail);
  Open(first, kIssues);
  Browser* second = CreateBrowser(profile);
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return TabAt(second, kMail) && InStep(first, second); }));
  WorkspaceList& list = WorkspaceList::Get();
  const std::string personal = list.Add(
      "Personal", std::string(list.UnusedIcon()), profile->GetBaseName());
  BrowserWindowInterface* partner = nullptr;
  WorkspaceWindow::From(first)->WithPartner(
      personal,
      base::BindOnce([](BrowserWindowInterface** partner,
                        BrowserWindowInterface* opened) { *partner = opened; },
                     &partner));
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return partner && partner->GetTabStripModel()->count() == 1; }));

  chrome::MoveTabsToExistingWindow(
      first, partner, {first->tab_strip_model()->GetIndexOfTab(mail)});
  Browser* other = partner->GetBrowserForMigrationOnly();
  ASSERT_TRUE(base::test::RunUntil([&] {
    return TabAt(other, kMail) && !TabAt(first, kMail) && !TabAt(second, kMail);
  }));
  EXPECT_FALSE(TabOrganizationController::IsCopy(TabAt(other, kMail)));
}

IN_PROC_BROWSER_TEST_F(SharedTabsBrowserTest,
                       CmdNOpensTheCommandBarOverABlankTabThatGoesUnused) {
  Browser* first = browser();
  Open(first, kMail);
  std::vector<BrowserWindowInterface*> known = GetAllBrowserWindowInterfaces();
  chrome::ExecuteCommand(first, IDC_NEW_WINDOW);
  Browser* fresh = nullptr;
  ASSERT_TRUE(base::test::RunUntil([&] {
    fresh = NewWindow(known);
    return fresh && TabAt(fresh, kMail) && BlankTabs(first) == 1 &&
           InStep(first, fresh);
  }));
  EXPECT_TRUE(base::test::RunUntil(
      [&] { return CommandBar::From(fresh)->IsShowing(); }));

  // Closed unused, the window takes its blank tab along.
  CloseBrowserSynchronously(fresh);
  ASSERT_TRUE(base::test::RunUntil([&] { return BlankTabs(first) == 0; }));

  // So does picking another tab in it.
  known = GetAllBrowserWindowInterfaces();
  chrome::ExecuteCommand(first, IDC_NEW_WINDOW);
  ASSERT_TRUE(base::test::RunUntil([&] {
    fresh = NewWindow(known);
    return fresh && TabAt(fresh, kMail) && BlankTabs(first) == 1;
  }));
  SelectByClick(fresh, TabAt(fresh, kMail));
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return BlankTabs(first) == 0 && BlankTabs(fresh) == 0; }));
}

// The command bar's list of open tabs, like tab search, has each page once,
// though every window shows it.
IN_PROC_BROWSER_TEST_F(SharedTabsBrowserTest, TabListsShowEachPageOnce) {
  Browser* first = browser();
  Open(first, kMail);
  Open(first, kIssues);
  Browser* second = CreateBrowser(first->GetProfile());
  ASSERT_TRUE(base::test::RunUntil([&] { return InStep(first, second); }));
  SelectByClick(second, TabAt(second, kIssues));
  ASSERT_TRUE(base::test::RunUntil([&] {
    return !TabOrganizationController::IsCopy(TabAt(second, kIssues));
  }));

  std::map<std::string, int> listed;
  for (const auto& tab : TabMatcherDesktop(nullptr, first->GetProfile())
                             .GetOpenTabs(nullptr,
                                          /*exclude_active_tab=*/false)) {
    ++listed[tab.url.spec()];
  }
  EXPECT_EQ(listed[kMail], 1);
  EXPECT_EQ(listed[kIssues], 1);
  int total = 0;
  for (const auto& [url, count] : listed) {
    total += count;
  }
  EXPECT_EQ(total, static_cast<int>(Layout(first).size()));
}

// A task folded in one window folds in the other, where the selected tab
// keeps its row, until the user picks another tab in it.
IN_PROC_BROWSER_TEST_F(SharedTabsBrowserTest, AFoldedTaskKeepsTheSelectedRow) {
  Browser* first = browser();
  Open(first, kMail);
  Open(first, kIssues);
  Browser* second = CreateBrowser(first->GetProfile());
  ASSERT_TRUE(base::test::RunUntil([&] { return InStep(first, second); }));
  const std::optional<PageId> mail =
      ControllerOf(first)->IdFor(TabAt(first, kMail));
  const std::optional<PageId> issues =
      ControllerOf(first)->IdFor(TabAt(first, kIssues));
  ASSERT_TRUE(mail && issues);
  ASSERT_TRUE(ControllerOf(first)->MoveToNewTask({*mail, *issues}, u"Trip"));
  ASSERT_TRUE(base::test::RunUntil([&] {
    return InStep(first, second) && TabAt(second, kIssues)->GetGroup();
  }));
  SelectByClick(second, TabAt(second, kIssues));

  TabStripModel* first_tabs = first->tab_strip_model();
  const tab_groups::TabGroupId task = *TabAt(first, kMail)->GetGroup();
  const tab_groups::TabGroupVisualData* visuals =
      first_tabs->group_model()->GetTabGroup(task)->visual_data();
  first_tabs->ChangeTabGroupVisuals(
      task, tab_groups::TabGroupVisualData(visuals->title(), visuals->color(),
                                           /*is_collapsed=*/true));
  ASSERT_TRUE(base::test::RunUntil([&] { return InStep(first, second); }));
  EXPECT_TRUE(second->tab_strip_model()
                  ->group_model()
                  ->GetTabGroup(*TabAt(second, kMail)->GetGroup())
                  ->visual_data()
                  ->is_collapsed());
  EXPECT_EQ(second->tab_strip_model()->GetActiveTab(), TabAt(second, kIssues));
  EXPECT_TRUE(base::test::RunUntil([&] {
    const views::View* kept = RowOf(second, TabAt(second, kIssues));
    const views::View* folded = RowOf(second, TabAt(second, kMail));
    return kept && kept->GetVisible() && !kept->size().IsEmpty() && folded &&
           !folded->GetVisible();
  }));
  SaveWindowScreenshot(second,
                       "shared-tabs-folded-task-keeps-the-selected-row");

  SelectByClick(second, TabAt(second, kMail));
  EXPECT_TRUE(base::test::RunUntil([&] {
    return InStep(first, second) && !first_tabs->IsGroupCollapsed(task);
  }));
}

// A branch folded in one window stays folded in the other, where the
// selected tab keeps its row, until the user picks another tab in it.
IN_PROC_BROWSER_TEST_F(SharedTabsBrowserTest,
                       AFoldedBranchKeepsTheSelectedRow) {
  Browser* first = browser();
  Open(first, kMail);
  Open(first, kIssues);
  Open(first, kIssue);
  Browser* second = CreateBrowser(first->GetProfile());
  ASSERT_TRUE(base::test::RunUntil([&] { return InStep(first, second); }));
  // A tab opened in the background goes right under the selected tab, so
  // Issue 412 is first, and Issues and Mail nest under it.
  TabOrganizationController* controller = ControllerOf(first);
  const std::optional<PageId> issue = controller->IdFor(TabAt(first, kIssue));
  ASSERT_TRUE(issue);
  ASSERT_TRUE(controller->Nest(*controller->IdFor(TabAt(first, kIssues))));
  ASSERT_TRUE(controller->Nest(*controller->IdFor(TabAt(first, kMail))));
  ASSERT_TRUE(base::test::RunUntil([&] {
    return InStep(first, second) &&
           ControllerOf(second)->DepthFor(TabAt(second, kMail)) == 1;
  }));
  SelectByClick(second, TabAt(second, kMail));

  controller->ToggleBranches({*issue});
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return ControllerOf(second)->IsCollapsed(*issue); }));
  EXPECT_FALSE(ControllerOf(second)->IsHidden(TabAt(second, kMail)));
  EXPECT_TRUE(ControllerOf(second)->IsHidden(TabAt(second, kIssues)));

  SelectByClick(second, TabAt(second, kIssues));
  EXPECT_TRUE(
      base::test::RunUntil([&] { return !controller->IsCollapsed(*issue); }));
}

class SharedTabsOffBrowserTest : public headless::HeadlessModeBrowserTest {};

// Close Task and Clear leave a blank tab instead of closing a window's last
// tabs. Pins do not count.
IN_PROC_BROWSER_TEST_F(SharedTabsOffBrowserTest,
                       CloseTaskAndClearLeaveABlankTabBesidePins) {
  Browser* window = browser();
  TabStripModel* tabs = window->tab_strip_model();
  TabOrganizationController* controller = ControllerOf(window);
  content::WebContents* pinned =
      chrome::AddAndReturnTabAt(window, GURL(kMail), -1, false);
  ASSERT_TRUE(content::WaitForLoadStop(pinned));
  tabs->SetTabPinned(tabs->GetIndexOfWebContents(pinned), true);
  const auto regular = [&] {
    return tabs->count() - tabs->IndexOfFirstNonPinnedTab();
  };
  ASSERT_EQ(regular(), 1);

  const std::optional<PageId> only =
      controller->IdFor(tabs->GetTabAtIndex(tabs->IndexOfFirstNonPinnedTab()));
  const std::optional<tab_groups::TabGroupId> task =
      controller->MoveToNewTask({*only}, u"Trip");
  ASSERT_TRUE(task);
  controller->CloseTask(*task);
  ASSERT_TRUE(base::test::RunUntil([&] {
    return regular() == 1 &&
           !tabs->GetTabAtIndex(tabs->IndexOfFirstNonPinnedTab())->GetGroup();
  }));

  const content::WebContents* blank =
      tabs->GetWebContentsAt(tabs->IndexOfFirstNonPinnedTab());
  controller->ClearScratchpad();
  ASSERT_TRUE(base::test::RunUntil([&] {
    return regular() == 1 &&
           tabs->GetWebContentsAt(tabs->IndexOfFirstNonPinnedTab()) != blank;
  }));
  EXPECT_TRUE(IsOpen(window));
}

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
