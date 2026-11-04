// Copyright 2026 The uw Authors. All rights reserved.

#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/run_loop.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/test/run_until.h"
#include "base/threading/thread_restrictions.h"
#include "build/build_config.h"
#include "chrome/app/chrome_command_ids.h"
#include "chrome/browser/headless/test/headless_mode_browsertest.h"
#include "chrome/browser/prefs/session_startup_pref.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/sessions/session_service_test_helper.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface_iterator.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/contents_container_view.h"
#include "chrome/browser/ui/views/frame/multi_contents_view.h"
#include "chrome/common/webui_url_constants.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/prefs/pref_service.h"
#include "components/sessions/core/session_id.h"
#include "components/sessions/core/session_types.h"
#include "components/split_tabs/split_tab_visual_data.h"
#include "components/tabs/public/split_tab_data.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "ui/base/accelerators/accelerator.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/views/bubble/bubble_dialog_delegate_view.h"
#include "ui/views/widget/widget.h"
#include "ui/views/widget/widget_delegate.h"
#include "url/gurl.h"
#include "uw/browser/agent/chat_tab.h"
#include "uw/browser/tab_organization/ai_connection.h"
#include "uw/browser/tab_organization/commands.h"
#include "uw/browser/terminal/terminal_tab.h"
#include "uw/browser/ui/views/command_bar/command_bar.h"
#include "uw/browser/ui/views/split_view/split_commands.h"
#include "uw/components/split_view/split_layout.h"

namespace uw {
namespace {

class SplitViewBrowserTest : public headless::HeadlessModeBrowserTest {
 protected:
  void SetUpOnMainThread() override {
    HeadlessModeBrowserTest::SetUpOnMainThread();
    ASSERT_TRUE(embedded_test_server()->Start());
    // A new pane shows the new tab page, as Cmd+T's tab does with the command
    // bar off.
    browser()->GetProfile()->GetPrefs()->SetBoolean(kCommandBarEnabledPref,
                                                    false);
    // Room for three panes of at least 200px across.
    browser()->GetWindow()->SetBounds(gfx::Rect(0, 0, 1400, 900));
  }

  TabStripModel* tabs() { return browser()->tab_strip_model(); }

  GURL Page(int number) {
    return embedded_test_server()->GetURL(
        "/title" + base::NumberToString(number) + ".html");
  }

  GURL Other() { return embedded_test_server()->GetURL("/simple.html"); }

  // Opens Other() in a tab of its own, in the background, after the split.
  tabs::TabInterface* OpenOther() {
    EXPECT_TRUE(ui_test_utils::NavigateToURLWithDisposition(
        browser(), Other(), WindowOpenDisposition::NEW_BACKGROUND_TAB,
        ui_test_utils::BROWSER_TEST_WAIT_FOR_LOAD_STOP));
    for (int i = 0; i < tabs()->count(); ++i) {
      if (tabs()->GetWebContentsAt(i)->GetVisibleURL() == Other()) {
        return tabs()->GetTabAtIndex(i);
      }
    }
    return nullptr;
  }

  // Shows `page` in `tab` and waits for it.
  void Show(tabs::TabInterface* tab, int page) {
    ASSERT_TRUE(content::NavigateToURL(tab->GetContents(), Page(page)));
  }

  void Run(int command) { chrome::ExecuteCommand(browser(), command); }

  std::vector<tabs::TabInterface*> SplitOf(tabs::TabInterface* tab) {
    return tab->GetSplit() ? tabs()->GetSplitData(*tab->GetSplit())->ListTabs()
                           : std::vector<tabs::TabInterface*>();
  }

  std::string LayoutOf(tabs::TabInterface* tab) {
    return tab->GetSplit() ? tabs()
                                 ->GetSplitData(*tab->GetSplit())
                                 ->visual_data()
                                 ->panes()
                                 .ToString()
                           : std::string();
  }

  // Where the page area shows `tab`, once it is laid out.
  gfx::Rect PaneOf(tabs::TabInterface* tab) {
    BrowserView* view = BrowserView::GetBrowserViewForBrowser(browser());
    view->GetWidget()->LayoutRootViewIfNecessary();
    ContentsContainerView* pane =
        view->multi_contents_view()->GetContentsContainerViewFor(
            tab->GetContents());
    return pane && pane->GetVisible() ? pane->GetBoundsInScreen() : gfx::Rect();
  }

  // What the last session saved: each window's tabs, marked "s" when in a
  // split, and each split's panes.
  std::string Saved() {
    std::vector<std::unique_ptr<sessions::SessionWindow>> windows;
    SessionID active = SessionID::InvalidValue();
    std::string platform_session_id;
    std::set<SessionID> discarded;
    SessionServiceTestHelper(browser()->GetProfile())
        .ReadWindows(&windows, &active, &platform_session_id, &discarded);
    std::string text;
    for (const auto& window : windows) {
      text += "window: selected " +
              base::NumberToString(window->selected_tab_index);
      for (const auto& tab : window->tabs) {
        text += tab->split_id ? " s" : " -";
      }
      for (const auto& split : window->split_tabs) {
        text += " " + split->split_visual_data_.panes().ToString();
      }
      text += "\n";
    }
    return text;
  }

  // Every window's tabs, marked "s" when in a split, the selected one "*".
  static std::string Shown() {
    std::string text;
    for (BrowserWindowInterface* window : GetAllBrowserWindowInterfaces()) {
      TabStripModel* model = window->GetTabStripModel();
      text += "window:";
      for (int i = 0; i < model->count(); ++i) {
        tabs::TabInterface* tab = model->GetTabAtIndex(i);
        text +=
            base::StrCat({" ", tab->GetContents()->GetVisibleURL().path()}) +
            (tab->IsSplit() ? "s" : "") +
            (i == model->active_index() ? "*" : "");
      }
      text += "\n";
    }
    return text;
  }

  static bool ShowsTheNewTabPage(tabs::TabInterface* tab) {
    const std::string_view host = tab->GetContents()->GetVisibleURL().host();
    return host == chrome::kChromeUINewTabHost ||
           host == chrome::kChromeUINewTabPageHost;
  }

  // A page on the left, beside a page on top of another: Cmd+D, then
  // Cmd+Shift+D, each new pane showing a page of its own.
  std::vector<tabs::TabInterface*> ThreePanes() {
    tabs::TabInterface* left = tabs()->GetActiveTab();
    Show(left, 1);
    Run(kSplitRight);
    tabs::TabInterface* top = tabs()->GetActiveTab();
    Show(top, 2);
    Run(kSplitDown);
    tabs::TabInterface* bottom = tabs()->GetActiveTab();
    Show(bottom, 3);
    return {left, top, bottom};
  }
};

// Cmd+D cuts the selected pane in two side by side, and the new tab on the
// right shows what Cmd+T gives. Cmd+Shift+D then cuts that pane in two, one
// above the other.
IN_PROC_BROWSER_TEST_F(SplitViewBrowserTest, CmdDThenCmdShiftDCutThePane) {
  ui::Accelerator key;
  BrowserView* view = BrowserView::GetBrowserViewForBrowser(browser());
  ASSERT_TRUE(view->GetAccelerator(kSplitRight, &key));
  ui::Accelerator down;
  ASSERT_TRUE(view->GetAccelerator(kSplitDown, &down));
#if BUILDFLAG(IS_MAC)
  EXPECT_EQ(key, ui::Accelerator(ui::VKEY_D, ui::EF_COMMAND_DOWN));
  EXPECT_EQ(down, ui::Accelerator(ui::VKEY_D,
                                  ui::EF_COMMAND_DOWN | ui::EF_SHIFT_DOWN));
#else
  EXPECT_EQ(key, ui::Accelerator(ui::VKEY_D, ui::EF_CONTROL_DOWN));
#endif

  tabs::TabInterface* left = tabs()->GetActiveTab();
  Show(left, 1);
  Run(kSplitRight);
  tabs::TabInterface* top = tabs()->GetActiveTab();
  ASSERT_NE(top, left);
  EXPECT_TRUE(ShowsTheNewTabPage(top));
  EXPECT_EQ(SplitOf(left), (std::vector{left, top}));
  EXPECT_EQ(LayoutOf(left), "r(0.5,0,1)");

  Run(kSplitDown);
  tabs::TabInterface* bottom = tabs()->GetActiveTab();
  EXPECT_TRUE(ShowsTheNewTabPage(bottom));
  EXPECT_EQ(SplitOf(left), (std::vector{left, top, bottom}));
  EXPECT_EQ(LayoutOf(left), "r(0.5,0,d(0.5,1,2))");
  EXPECT_TRUE(base::test::RunUntil([&] {
    const gfx::Rect l = PaneOf(left);
    const gfx::Rect t = PaneOf(top);
    const gfx::Rect b = PaneOf(bottom);
    return !l.IsEmpty() && !t.IsEmpty() && !b.IsEmpty() && t.x() > l.right() &&
           b.x() == t.x() && b.y() > t.bottom() && t.y() == l.y() &&
           b.bottom() == l.bottom();
  }));
}

// With the command bar on, it opens over the new pane.
IN_PROC_BROWSER_TEST_F(SplitViewBrowserTest, TheCommandBarOpensOverTheNewPane) {
  browser()->GetProfile()->GetPrefs()->SetBoolean(kCommandBarEnabledPref, true);
  Show(tabs()->GetActiveTab(), 1);
  Run(kSplitRight);
  CommandBar* bar = CommandBar::From(browser());
  ASSERT_TRUE(bar->IsShowing());
  const gfx::Rect pane = PaneOf(tabs()->GetActiveTab());
  // The panel may be moved to fit the screen, so its anchor is what tells.
  const gfx::Rect anchor = views::Widget::GetTopLevelWidgetForNativeView(
                               bar->page()->GetNativeView())
                               ->widget_delegate()
                               ->AsBubbleDialogDelegate()
                               ->GetAnchorRect();
  EXPECT_NEAR(anchor.CenterPoint().x(), pane.CenterPoint().x(), 2);
  EXPECT_GT(anchor.y(), pane.y());
}

// From a terminal, the new pane is a terminal in the same folder. From a chat
// it is what Cmd+T gives.
IN_PROC_BROWSER_TEST_F(SplitViewBrowserTest, ATerminalSplitsIntoTheSameFolder) {
  base::ScopedTempDir folder;
  {
    base::ScopedAllowBlockingForTesting allow_blocking;
    ASSERT_TRUE(folder.CreateUniqueTempDir());
  }
  OpenTerminalTabIn(browser(), folder.GetPath());
  tabs::TabInterface* terminal = tabs()->GetActiveTab();
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return !TerminalFolder(terminal->GetContents()).empty(); }));
  const base::FilePath shell_folder = TerminalFolder(terminal->GetContents());
  EXPECT_EQ(shell_folder.BaseName(), folder.GetPath().BaseName());

  Run(kSplitRight);
  tabs::TabInterface* pane = tabs()->GetActiveTab();
  ASSERT_NE(pane, terminal);
  EXPECT_EQ(SplitOf(terminal), (std::vector{terminal, pane}));
  EXPECT_TRUE(IsTerminalUrl(pane->GetContents()->GetVisibleURL()));
  EXPECT_TRUE(base::test::RunUntil(
      [&] { return TerminalFolder(pane->GetContents()) == shell_folder; }));

  OpenChatTab(browser());
  tabs::TabInterface* chat = tabs()->GetActiveTab();
  ASSERT_TRUE(content::WaitForLoadStop(chat->GetContents()));
  Run(kSplitRight);
  EXPECT_NE(tabs()->GetActiveTab(), chat);
  EXPECT_TRUE(ShowsTheNewTabPage(tabs()->GetActiveTab()));
  EXPECT_TRUE(chat->IsSplit());
}

// A split that would leave a pane narrower or shorter than 200px does
// nothing.
IN_PROC_BROWSER_TEST_F(SplitViewBrowserTest, NoPaneGetsSmallerThan200px) {
  browser()->GetWindow()->SetBounds(gfx::Rect(0, 0, 1000, 440));
  tabs::TabInterface* left = tabs()->GetActiveTab();
  Show(left, 1);
  ASSERT_TRUE(CanRunSplitCommand(left, kSplitRight));
  Run(kSplitRight);
  tabs::TabInterface* right = tabs()->GetActiveTab();
  ASSERT_TRUE(base::test::RunUntil([&] { return !PaneOf(right).IsEmpty(); }));
  const gfx::Rect pane = PaneOf(right);
  ASSERT_GE(pane.width(), kMinPaneSize);
  ASSERT_LT(pane.width() / 2, kMinPaneSize);
  ASSERT_LT(pane.height() / 2, kMinPaneSize);

  const int count = tabs()->count();
  EXPECT_FALSE(CanRunSplitCommand(right, kSplitRight));
  EXPECT_FALSE(CanRunSplitCommand(right, kSplitDown));
  Run(kSplitRight);
  Run(kSplitDown);
  EXPECT_EQ(tabs()->count(), count);
  EXPECT_EQ(LayoutOf(left), "r(0.5,0,1)");
}

// Ctrl+Tab steps through the panes in reading order, then leaves the split.
IN_PROC_BROWSER_TEST_F(SplitViewBrowserTest, CtrlTabStepsThroughThePanes) {
  const std::vector<tabs::TabInterface*> panes = ThreePanes();
  tabs::TabInterface* after = OpenOther();
  ASSERT_TRUE(after);
  ASSERT_EQ(tabs()->GetIndexOfTab(after), 3);
  tabs()->ActivateTabAt(tabs()->GetIndexOfTab(panes[0]));

  for (tabs::TabInterface* next : {panes[1], panes[2], after}) {
    Run(IDC_SELECT_NEXT_TAB);
    EXPECT_EQ(tabs()->GetActiveTab(), next);
  }
  Run(IDC_SELECT_PREVIOUS_TAB);
  EXPECT_EQ(tabs()->GetActiveTab(), panes[2]);
}

// Cmd+W closes the selected pane, and its neighbor takes its space. Move Out
// of Split View keeps the tab open just below the split. Separate Panes ends
// the split and leaves its tabs in reading order.
IN_PROC_BROWSER_TEST_F(SplitViewBrowserTest, ClosingAndEndingASplit) {
  std::vector<tabs::TabInterface*> panes = ThreePanes();
  tabs()->ActivateTabAt(tabs()->GetIndexOfTab(panes[1]));
  Run(IDC_CLOSE_TAB);
  ASSERT_TRUE(base::test::RunUntil([&] { return tabs()->count() == 2; }));
  EXPECT_EQ(SplitOf(panes[0]), (std::vector{panes[0], panes[2]}));
  EXPECT_EQ(LayoutOf(panes[0]), "r(0.5,0,1)");
  EXPECT_EQ(tabs()->GetActiveTab(), panes[2]);
  EXPECT_TRUE(base::test::RunUntil(
      [&] { return PaneOf(panes[2]).height() == PaneOf(panes[0]).height(); }));

  Run(kSplitDown);
  tabs::TabInterface* added = tabs()->GetActiveTab();
  Show(added, 2);
  ASSERT_EQ(SplitOf(panes[0]), (std::vector{panes[0], panes[2], added}));
  RunSplitCommand(panes[2], kMoveOutOfSplit);
  EXPECT_FALSE(panes[2]->IsSplit());
  EXPECT_EQ(SplitOf(panes[0]), (std::vector{panes[0], added}));
  EXPECT_EQ(tabs()->GetIndexOfTab(panes[2]), tabs()->GetIndexOfTab(added) + 1);

  RunSplitCommand(panes[2], kSplitDown);
  tabs::TabInterface* last = tabs()->GetActiveTab();
  Show(last, 3);
  tabs()->ActivateTabAt(tabs()->GetIndexOfTab(panes[0]));
  RunSplitCommand(panes[0], kSplitDown);
  ASSERT_EQ(SplitOf(panes[0]).size(), 3u);
  const std::vector<tabs::TabInterface*> order = SplitOf(panes[0]);
  Run(kSeparatePanes);
  for (size_t i = 0; i < order.size(); ++i) {
    EXPECT_FALSE(order[i]->IsSplit());
    EXPECT_EQ(tabs()->GetTabAtIndex(static_cast<int>(i)), order[i]);
  }
  EXPECT_FALSE(CanRunSplitCommand(panes[0], kSeparatePanes));
}

// Reopen Closed Split View brings back the whole layout, and the pane that
// had focus.
IN_PROC_BROWSER_TEST_F(SplitViewBrowserTest, ReopeningAClosedSplitView) {
  const std::vector<tabs::TabInterface*> panes = ThreePanes();
  const split_tabs::SplitTabId split = *panes[0]->GetSplit();
  tabs()->UpdateSplitCutRatio(split, 0, 0.3);
  tabs()->UpdateSplitCutRatio(split, 1, 0.7);
  ASSERT_EQ(LayoutOf(panes[0]), "r(0.3,0,d(0.7,1,2))");
  tabs()->ActivateTabAt(tabs()->GetIndexOfTab(panes[1]));
  ASSERT_TRUE(OpenOther());

  // Close Tab on a tab in the split, which is selected, closes the split.
  tabs()->ExecuteContextMenuCommand(tabs()->GetIndexOfTab(panes[1]),
                                    TabStripModel::CommandCloseTab);
  ASSERT_TRUE(base::test::RunUntil([&] { return tabs()->count() == 1; }));

  chrome::RestoreTab(browser());
  ASSERT_TRUE(base::test::RunUntil([&] {
    return tabs()->count() == 4 && tabs()->GetActiveTab()->IsSplit();
  }));
  tabs::TabInterface* focused = tabs()->GetActiveTab();
  EXPECT_EQ(LayoutOf(focused), "r(0.3,0,d(0.7,1,2))");
  EXPECT_EQ(focused->GetContents()->GetVisibleURL(), Page(2));
  const std::vector<tabs::TabInterface*> restored = SplitOf(focused);
  ASSERT_EQ(restored.size(), 3u);
  EXPECT_EQ(restored[0]->GetContents()->GetVisibleURL(), Page(1));
  EXPECT_EQ(restored[2]->GetContents()->GetVisibleURL(), Page(3));
}

IN_PROC_BROWSER_TEST_F(SplitViewBrowserTest,
                       PRE_TheLayoutComesBackAfterARestart) {
  SessionStartupPref::SetStartupPref(
      browser()->GetProfile(), SessionStartupPref(SessionStartupPref::LAST));
  const std::vector<tabs::TabInterface*> panes = ThreePanes();
  const split_tabs::SplitTabId split = *panes[0]->GetSplit();
  tabs()->UpdateSplitCutRatio(split, 0, 0.3);
  tabs()->UpdateSplitCutRatio(split, 1, 0.7);
  tabs()->ActivateTabAt(tabs()->GetIndexOfTab(panes[1]));
  ASSERT_EQ(LayoutOf(panes[0]), "r(0.3,0,d(0.7,1,2))");
  // Lets the session take the tabs' data before the browser quits.
  base::RunLoop().RunUntilIdle();
}

// After a restart the split comes back with its panes and their directions
// and sizes. The session keeps the pane that had focus as the window's
// selected tab, which a launch selects again; a test opens a blank tab in
// front of it.
IN_PROC_BROWSER_TEST_F(SplitViewBrowserTest, TheLayoutComesBackAfterARestart) {
  const std::string saved = Saved();
  EXPECT_EQ(saved, "window: selected 1 s s s r(0.3,0,d(0.7,1,2))\n");
  ASSERT_TRUE(base::test::RunUntil([&] {
    return tabs()->count() > 0 && tabs()->GetTabAtIndex(0)->IsSplit();
  })) << Shown();
  const std::vector<tabs::TabInterface*> panes =
      SplitOf(tabs()->GetTabAtIndex(0));
  ASSERT_EQ(panes.size(), 3u);
  EXPECT_EQ(LayoutOf(panes[0]), "r(0.3,0,d(0.7,1,2))");
  for (int pane = 0; pane < 3; ++pane) {
    // The test server's port changes with the restart.
    EXPECT_EQ(panes[pane]->GetContents()->GetVisibleURL().path(),
              Page(pane + 1).path());
  }
}

}  // namespace
}  // namespace uw
