// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/browser/little_window/little_window.h"

#include <string>
#include <string_view>

#include "base/run_loop.h"
#include "base/task/sequenced_task_runner.h"
#include "base/test/run_until.h"
#include "base/time/time.h"
#include "chrome/app/chrome_command_ids.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/headless/test/headless_mode_browsertest.h"
#include "chrome/browser/prefs/session_startup_pref.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/browser/ui/browser_window/public/browser_window_features.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface_iterator.h"
#include "chrome/browser/ui/navigator/browser_navigator.h"
#include "chrome/browser/ui/navigator/browser_navigator_params.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/location_bar/location_bar_view.h"
#include "chrome/browser/ui/views/toolbar/toolbar_view.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/omnibox/browser/location_bar_model.h"
#include "components/prefs/pref_service.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "ui/base/window_open_disposition.h"
#include "ui/views/widget/widget.h"
#include "url/gurl.h"
#include "uw/browser/little_window/little_window_key.h"
#include "uw/browser/little_window/little_window_mark.h"
#include "uw/browser/little_window/little_window_prefs.h"
#include "uw/browser/tab_organization/tab_organization_controller.h"
#include "uw/browser/tab_organization/window_screenshot.h"
#include "uw/browser/ui/views/command_bar/command_bar.h"
#include "uw/browser/workspaces/workspace_window.h"

namespace uw {
namespace {

class LittleWindowBrowserTest : public headless::HeadlessModeBrowserTest {
 protected:
  void SetUpOnMainThread() override {
    HeadlessModeBrowserTest::SetUpOnMainThread();
    ASSERT_TRUE(embedded_test_server()->Start());
  }

  GURL Page(std::string_view path) {
    return embedded_test_server()->GetURL(path);
  }

  // The little window open now, or null.
  BrowserWindowInterface* Little() {
    BrowserWindowInterface* found = nullptr;
    ForEachCurrentBrowserWindowInterfaceOrderedByActivation(
        [&](BrowserWindowInterface* window) {
          if (IsLittleWindow(window)) {
            found = window;
            return false;
          }
          return true;
        });
    return found;
  }

  // Opens `url` in a little window and waits for it to load.
  BrowserWindowInterface* OpenWithPage(const GURL& url) {
    BrowserWindowInterface* little = OpenLittleWindow(url);
    EXPECT_TRUE(little);
    if (little) {
      EXPECT_TRUE(content::WaitForLoadStop(
          little->GetTabStripModel()->GetActiveWebContents()));
    }
    return little;
  }

  // Where `page` is among the Scratchpad's tabs of `window`, the tabs in no
  // task, counting from the top, or -1.
  int ScratchpadPlace(BrowserWindowInterface* window,
                      content::WebContents* page) {
    TabStripModel* tabs = window->GetTabStripModel();
    int place = 0;
    for (int i = tabs->IndexOfFirstNonPinnedTab(); i < tabs->count(); ++i) {
      if (tabs->GetTabGroupForTab(i)) {
        continue;
      }
      if (tabs->GetWebContentsAt(i) == page) {
        return place;
      }
      ++place;
    }
    return -1;
  }

  void Settle() {
    base::RunLoop loop;
    base::SequencedTaskRunner::GetCurrentDefault()->PostDelayedTask(
        FROM_HERE, loop.QuitClosure(), base::Milliseconds(300));
    loop.Run();
  }
};

IN_PROC_BROWSER_TEST_F(LittleWindowBrowserTest,
                       TheCommandOpensABlankLittleWindowWithTheCommandBar) {
  chrome::ExecuteCommand(browser(), kNewLittleWindowCommand);
  ASSERT_TRUE(base::test::RunUntil([&] { return Little(); }));
  BrowserWindowInterface* little = Little();

  EXPECT_EQ(BrowserWindowInterface::TYPE_POPUP, little->GetType());
  EXPECT_EQ(WorkspaceWindow::From(browser())->workspace(),
            LittleWindowWorkspace(little));
  ASSERT_EQ(1, little->GetTabStripModel()->count());
  EXPECT_TRUE(little->GetTabStripModel()
                  ->GetActiveWebContents()
                  ->GetVisibleURL()
                  .IsAboutBlank());
  // Its address is empty, as a new tab's is.
  EXPECT_EQ(u"",
            little->GetFeatures().location_bar_model()->GetURLForDisplay());
  ASSERT_TRUE(CommandBar::From(little));
  EXPECT_TRUE(base::test::RunUntil(
      [&] { return CommandBar::From(little)->IsShowing(); }));
  // It is none of the organizer's, and has no workspace window of its own.
  EXPECT_FALSE(TabOrganizationController::From(little));
  EXPECT_FALSE(WorkspaceWindow::From(little));
}

IN_PROC_BROWSER_TEST_F(LittleWindowBrowserTest,
                       ItHasARegularToolbarWithAnAddressThatChanges) {
  BrowserWindowInterface* little = OpenWithPage(Page("/title1.html"));
  ASSERT_TRUE(little);
  BrowserView* view = BrowserView::GetBrowserViewForBrowser(little);
  ASSERT_TRUE(view->toolbar());
  // Back, forward, and reload come with the regular toolbar only.
  EXPECT_TRUE(view->toolbar()->reload_button());
  ASSERT_TRUE(view->GetLocationBarView());
  EXPECT_FALSE(view->ShouldDrawVerticalTabStrip());
  EXPECT_NE(u"",
            little->GetFeatures().location_bar_model()->GetURLForDisplay());
  SaveWindowScreenshot(little->GetBrowserForMigrationOnly(),
                       "little-window-page");
}

IN_PROC_BROWSER_TEST_F(LittleWindowBrowserTest,
                       EscBeforeAPickClosesTheEmptyWindow) {
  BrowserWindowInterface* little = OpenLittleWindow();
  ASSERT_TRUE(little);
  CommandBar* bar = CommandBar::From(little);
  ASSERT_TRUE(base::test::RunUntil([&] { return bar->IsShowing(); }));
  ASSERT_TRUE(content::WaitForLoadStop(bar->page()));

  views::Widget::GetTopLevelWidgetForNativeView(bar->page()->GetNativeView())
      ->CloseWithReason(views::Widget::ClosedReason::kEscKeyPressed);

  EXPECT_TRUE(base::test::RunUntil([&] { return !Little(); }));
}

IN_PROC_BROWSER_TEST_F(LittleWindowBrowserTest,
                       AClickElsewhereKeepsTheEmptyWindow) {
  BrowserWindowInterface* little = OpenLittleWindow();
  ASSERT_TRUE(little);
  CommandBar* bar = CommandBar::From(little);
  ASSERT_TRUE(base::test::RunUntil([&] { return bar->IsShowing(); }));
  ASSERT_TRUE(content::WaitForLoadStop(bar->page()));

  views::Widget::GetTopLevelWidgetForNativeView(bar->page()->GetNativeView())
      ->CloseWithReason(views::Widget::ClosedReason::kLostFocus);
  ASSERT_TRUE(base::test::RunUntil([&] { return !bar->IsShowing(); }));
  Settle();

  EXPECT_EQ(little, Little());
}

IN_PROC_BROWSER_TEST_F(LittleWindowBrowserTest,
                       CmdOMovesTheLivePageToTheTopOfTheScratchpad) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), Page("/title2.html")));
  BrowserWindowInterface* little = OpenWithPage(Page("/title1.html"));
  ASSERT_TRUE(little);
  content::WebContents* page =
      little->GetTabStripModel()->GetActiveWebContents();
  ASSERT_TRUE(content::ExecJs(page, "window.kept = 'still here';"));

  chrome::ExecuteCommand(little, IDC_OPEN_FILE);

  ASSERT_TRUE(base::test::RunUntil([&] { return !Little(); }));
  TabStripModel* tabs = browser()->tab_strip_model();
  ASSERT_TRUE(base::test::RunUntil([&] {
    return tabs->GetActiveWebContents() == page &&
           ScratchpadPlace(browser(), page) == 0;
  }));
  // The same page, not a reload of its address.
  EXPECT_EQ("still here", content::EvalJs(page, "window.kept"));
}

IN_PROC_BROWSER_TEST_F(LittleWindowBrowserTest,
                       ALinkForANewTabOpensBehindAtTheTopOfTheScratchpad) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), Page("/title2.html")));
  BrowserWindowInterface* little = OpenWithPage(Page("/title1.html"));
  ASSERT_TRUE(little);
  const int count = browser()->tab_strip_model()->count();

  NavigateParams params(little, Page("/title3.html"), ui::PAGE_TRANSITION_LINK);
  params.disposition = WindowOpenDisposition::NEW_BACKGROUND_TAB;
  params.source_contents = little->GetTabStripModel()->GetActiveWebContents();
  Navigate(&params);

  ASSERT_TRUE(params.navigated_or_inserted_contents);
  EXPECT_EQ(browser(), params.browser);
  ASSERT_TRUE(base::test::RunUntil([&] {
    return browser()->tab_strip_model()->count() == count + 1 &&
           ScratchpadPlace(browser(), params.navigated_or_inserted_contents) ==
               0;
  }));
  EXPECT_EQ(1, little->GetTabStripModel()->count());
  EXPECT_EQ(little, Little());
}

IN_PROC_BROWSER_TEST_F(LittleWindowBrowserTest,
                       CmdShiftTBringsItBackAsALittleWindow) {
  const GURL url = Page("/title1.html");
  BrowserWindowInterface* little = OpenWithPage(url);
  ASSERT_TRUE(little);
  const std::string workspace = LittleWindowWorkspace(little);
  little->GetWindow()->Close();
  ASSERT_TRUE(base::test::RunUntil([&] { return !Little(); }));

  chrome::ExecuteCommand(browser(), IDC_RESTORE_TAB);

  ASSERT_TRUE(base::test::RunUntil([&] { return Little(); }));
  EXPECT_EQ(workspace, LittleWindowWorkspace(Little()));
  EXPECT_EQ(BrowserWindowInterface::TYPE_POPUP, Little()->GetType());
  EXPECT_TRUE(base::test::RunUntil([&] {
    return Little()
               ->GetTabStripModel()
               ->GetActiveWebContents()
               ->GetVisibleURL() == url;
  }));
}

IN_PROC_BROWSER_TEST_F(LittleWindowBrowserTest,
                       PRE_ItDoesNotComeBackAfterAQuit) {
  SessionStartupPref::SetStartupPref(
      browser()->GetProfile(), SessionStartupPref(SessionStartupPref::LAST));
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), Page("/title2.html")));
  ASSERT_TRUE(OpenWithPage(Page("/title1.html?little")));
}

IN_PROC_BROWSER_TEST_F(LittleWindowBrowserTest, ItDoesNotComeBackAfterAQuit) {
  Settle();
  EXPECT_FALSE(Little());
  for (BrowserWindowInterface* window : GetAllBrowserWindowInterfaces()) {
    TabStripModel* tabs = window->GetTabStripModel();
    for (int i = 0; i < tabs->count(); ++i) {
      EXPECT_EQ(
          std::string::npos,
          tabs->GetWebContentsAt(i)->GetVisibleURL().spec().find("little"));
    }
  }
}

IN_PROC_BROWSER_TEST_F(LittleWindowBrowserTest,
                       AnUnusedLittleWindowClosesAfterTheSetTime) {
  BrowserWindowInterface* little = OpenWithPage(Page("/title1.html"));
  ASSERT_TRUE(little);
  g_browser_process->local_state()->SetInteger(kLittleWindowCloseAfterPref, 60);
  LittleWindow* window = LittleWindow::From(little);
  window->SetInFrontForTesting(false);

  window->SetLastActiveForTesting(base::TimeTicks::Now() - base::Minutes(59));
  window->CloseIfUnused();
  Settle();
  ASSERT_EQ(little, Little());

  window->SetLastActiveForTesting(base::TimeTicks::Now() - base::Minutes(61));
  window->CloseIfUnused();
  EXPECT_TRUE(base::test::RunUntil([&] { return !Little(); }));
}

IN_PROC_BROWSER_TEST_F(LittleWindowBrowserTest,
                       APageThatAsksBeforeItIsLeftKeepsItsWindow) {
  BrowserWindowInterface* little = OpenWithPage(Page("/title1.html"));
  ASSERT_TRUE(little);
  content::WebContents* page =
      little->GetTabStripModel()->GetActiveWebContents();
  ASSERT_TRUE(
      content::ExecJs(page, "onbeforeunload = e => e.preventDefault();"));
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return page->NeedToFireBeforeUnloadOrUnloadEvents(); }));
  LittleWindow::From(little)->SetInFrontForTesting(false);
  g_browser_process->local_state()->SetInteger(kLittleWindowCloseAfterPref, 60);
  LittleWindow::From(little)->SetLastActiveForTesting(base::TimeTicks::Now() -
                                                      base::Hours(5));

  LittleWindow::From(little)->CloseIfUnused();
  Settle();

  EXPECT_EQ(little, Little());
  // So the browser can quit when the test ends.
  ASSERT_TRUE(content::ExecJs(page, "onbeforeunload = null;"));
}

IN_PROC_BROWSER_TEST_F(LittleWindowBrowserTest,
                       LinksFromOtherAppsOpenInLittleWindowsOnlyWithTheSwitch) {
  const GURL url = Page("/title1.html");
  EXPECT_FALSE(OpenLinkFromAnotherAppInLittleWindow(url));
  EXPECT_FALSE(Little());

  g_browser_process->local_state()->SetBoolean(kLittleWindowLinksPref, true);
  EXPECT_TRUE(OpenLinkFromAnotherAppInLittleWindow(url));
  ASSERT_TRUE(base::test::RunUntil([&] { return Little(); }));
  EXPECT_EQ(
      url,
      Little()->GetTabStripModel()->GetActiveWebContents()->GetVisibleURL());
}

IN_PROC_BROWSER_TEST_F(LittleWindowBrowserTest,
                       TheMenuOpensOneWhateverTheKeySwitch) {
  g_browser_process->local_state()->SetBoolean(kLittleWindowKeyInUwPref, false);
  RunNewLittleWindowCommand();
  EXPECT_TRUE(base::test::RunUntil([&] { return Little(); }));
}

}  // namespace
}  // namespace uw
