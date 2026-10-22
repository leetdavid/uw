// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include <algorithm>
#include <map>
#include <string>
#include <vector>

#include "base/functional/function_ref.h"
#include "base/run_loop.h"
#include "base/test/run_until.h"
#include "chrome/app/chrome_command_ids.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/headless/test/headless_mode_browsertest.h"
#include "chrome/browser/prefs/session_startup_pref.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/profiles/profile_test_util.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/browser/ui/browser_window/public/browser_window_features.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface_iterator.h"
#include "chrome/browser/ui/tabs/tab_menu_model.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/tabs/common/tab_view.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "ui/events/base_event_utils.h"
#include "ui/events/event.h"
#include "ui/views/controls/textfield/textfield.h"
#include "ui/views/view_utils.h"
#include "ui/views/widget/widget.h"
#include "url/gurl.h"
#include "uw/browser/pins/pin_store.h"
#include "uw/browser/pins/pin_sync.h"
#include "uw/browser/ui/views/pins/home_url_card.h"
#include "uw/browser/ui/views/pins/pin_menu.h"
#include "uw/browser/workspaces/workspace_list.h"
#include "uw/browser/workspaces/workspace_window.h"

namespace uw {
namespace {

constexpr char kMail[] = "chrome://version/?mail";
constexpr char kIssues[] = "chrome://version/?issues";
constexpr char kIssue[] = "chrome://version/?issue-412";
// A page that remembers a half-typed field, a number drawn when it loaded,
// and a counter that keeps going.
constexpr char kNotes[] =
    "data:text/html,<title>Notes</title><input id=f><script>token="
    "Math.random();ticks=0;setInterval(()=>ticks++,10)</script>";
constexpr char kTypeIntoNotes[] =
    "document.getElementById('f').value = 'half-typed'";
constexpr char kNotesState[] =
    "document.getElementById('f').value + ' ' + token";

class PinsBrowserTest : public headless::HeadlessModeBrowserTest {
 protected:
  // Opens `url` in a new tab of `window` and pins it.
  tabs::TabInterface* Pin(Browser* window, const GURL& url) {
    TabStripModel* tabs = window->tab_strip_model();
    chrome::AddTabAt(window, url, -1, true);
    tabs::TabInterface* tab = tabs->GetActiveTab();
    EXPECT_TRUE(content::WaitForLoadStop(tab->GetContents()));
    tabs->SetTabPinned(tabs->GetIndexOfTab(tab), true);
    return tab;
  }

  static std::vector<GURL> PinnedUrls(Browser* window) {
    std::vector<GURL> urls;
    TabStripModel* tabs = window->tab_strip_model();
    for (int i = 0; i < tabs->IndexOfFirstNonPinnedTab(); ++i) {
      urls.push_back(tabs->GetWebContentsAt(i)->GetVisibleURL());
    }
    return urls;
  }

  // Waits until `window` shows exactly `urls` as its pins, in that order.
  static bool ShowsPins(Browser* window, const std::vector<GURL>& urls) {
    return base::test::RunUntil(
        [&] { return PinnedUrls(window) == urls; });
  }

  PinStore* store() { return PinStore::For(browser()->GetProfile()); }

  static content::WebContents* PinPageOf(Browser* window) {
    return window->tab_strip_model()->GetWebContentsAt(0);
  }

  // Waits until the first pin of `window` is `page`.
  static bool Holds(Browser* window, content::WebContents* page) {
    return base::test::RunUntil([&] { return PinPageOf(window) == page; });
  }

  static bool HasRenderer(content::WebContents* page) {
    return page->GetPrimaryMainFrame()->IsRenderFrameLive();
  }

  static std::string StateOf(content::WebContents* page) {
    return content::EvalJs(page, kNotesState).ExtractString();
  }

  static int TicksOf(content::WebContents* page) {
    return content::EvalJs(page, "ticks").ExtractInt();
  }

  static void Navigate(tabs::TabInterface* tab, const GURL& url) {
    ASSERT_TRUE(content::NavigateToURL(tab->GetContents(), url));
  }

  static views::View* FindTabView(views::View* view,
                                  const tabs::TabInterface* tab) {
    if (auto* tab_view = views::AsViewClass<TabView>(view);
        tab_view && tab_view->GetTabInterface() == tab) {
      return tab_view;
    }
    for (views::View* child : view->children()) {
      if (views::View* found = FindTabView(child, tab)) {
        return found;
      }
    }
    return nullptr;
  }

  static bool AllShow(const std::vector<BrowserWindowInterface*>& windows,
                      const std::vector<GURL>& urls) {
    return std::ranges::all_of(windows, [&](BrowserWindowInterface* window) {
      return PinnedUrls(window->GetBrowserForMigrationOnly()) == urls;
    });
  }

  // Pins kMail in a second window too, and switches the first window to a
  // second workspace on the profile, so a hidden window has a copy as well.
  // The next start restores all three windows with their copies.
  void PinInThreeWindowsBeforeARestart() {
    Profile* profile = browser()->GetProfile();
    SessionStartupPref::SetStartupPref(
        profile, SessionStartupPref(SessionStartupPref::LAST));
    WorkspaceList& list = WorkspaceList::Get();
    const std::string reading = list.Add(
        "Reading", std::string(list.UnusedIcon()), profile->GetBaseName());
    Pin(browser(), GURL(kMail));
    CreateBrowser(profile);
    WorkspaceWindow::From(browser())->Switch(reading);
    ASSERT_TRUE(base::test::RunUntil([&] {
      return GetAllBrowserWindowInterfaces().size() == 3u &&
             AllShow(GetAllBrowserWindowInterfaces(), {GURL(kMail)});
    }));
    // Lets the session take the tabs' data before the browser quits.
    base::RunLoop().RunUntilIdle();
  }

  // Unpins the restored kMail in a window that shows, with `unpin`. The live
  // page stays a regular tab in the window that holds it, every other window
  // loses its copy, and no window gains a tab.
  void UnpinAfterARestart(base::FunctionRef<void(TabStripModel*)> unpin) {
    std::vector<BrowserWindowInterface*> windows;
    ASSERT_TRUE(base::test::RunUntil([&] {
      windows = GetAllBrowserWindowInterfaces();
      return windows.size() == 3u && AllShow(windows, {GURL(kMail)});
    }));
    base::RunLoop().RunUntilIdle();
    BrowserWindowInterface* shown = nullptr;
    std::map<BrowserWindowInterface*, int> before;
    for (BrowserWindowInterface* window : windows) {
      before[window] = window->GetTabStripModel()->count();
      if (!WorkspaceWindow::IsHidden(window)) {
        shown = window;
      }
    }
    ASSERT_TRUE(shown);

    unpin(shown->GetTabStripModel());
    for (int i = 0; i < 5; ++i) {
      base::RunLoop().RunUntilIdle();
    }

    EXPECT_TRUE(store()->pins().empty());
    EXPECT_TRUE(AllShow(windows, {}));
    int keeping = 0;
    for (BrowserWindowInterface* window : windows) {
      const int count = window->GetTabStripModel()->count();
      EXPECT_TRUE(count == before[window] || count == before[window] - 1);
      keeping += count == before[window];
    }
    EXPECT_EQ(keeping, 1);
  }

  views::View* TileOf(const tabs::TabInterface* tab) {
    return FindTabView(BrowserView::GetBrowserViewForBrowser(browser()), tab);
  }
};

IN_PROC_BROWSER_TEST_F(PinsBrowserTest, APinShowsInEveryWindow) {
  Browser* second = CreateBrowser(browser()->GetProfile());
  Pin(browser(), GURL(kMail));
  Pin(browser(), GURL(kIssues));
  EXPECT_TRUE(ShowsPins(second, {GURL(kMail), GURL(kIssues)}));
  EXPECT_TRUE(ShowsPins(browser(), {GURL(kMail), GURL(kIssues)}));
  ASSERT_EQ(store()->pins().size(), 2u);
  EXPECT_EQ(store()->pins()[0].home, GURL(kMail));
}

IN_PROC_BROWSER_TEST_F(PinsBrowserTest, ANewWindowGetsThePins) {
  Pin(browser(), GURL(kMail));
  base::RunLoop().RunUntilIdle();
  Browser* second = CreateBrowser(browser()->GetProfile());
  EXPECT_TRUE(ShowsPins(second, {GURL(kMail)}));
}

IN_PROC_BROWSER_TEST_F(PinsBrowserTest, UnpinningRemovesItEverywhere) {
  Browser* second = CreateBrowser(browser()->GetProfile());
  content::WebContents* page = Pin(browser(), GURL(kMail))->GetContents();
  ASSERT_TRUE(ShowsPins(second, {GURL(kMail)}));
  const int tabs_before = browser()->tab_strip_model()->count();
  const int second_before = second->tab_strip_model()->count();

  // Unpinned in a window that only has a placeholder: the live page stays a
  // regular tab in the window that holds it.
  second->tab_strip_model()->SetTabPinned(0, false);

  EXPECT_TRUE(ShowsPins(browser(), {}));
  EXPECT_TRUE(ShowsPins(second, {}));
  EXPECT_TRUE(base::test::RunUntil(
      [&] { return second->tab_strip_model()->count() == second_before - 1; }));
  EXPECT_EQ(browser()->tab_strip_model()->count(), tabs_before);
  EXPECT_NE(browser()->tab_strip_model()->GetIndexOfWebContents(page),
            TabStripModel::kNoTab);
  EXPECT_TRUE(store()->pins().empty());
}

IN_PROC_BROWSER_TEST_F(PinsBrowserTest, UnpinningTheLivePageKeepsItThere) {
  Browser* second = CreateBrowser(browser()->GetProfile());
  content::WebContents* page = Pin(browser(), GURL(kMail))->GetContents();
  ASSERT_TRUE(ShowsPins(second, {GURL(kMail)}));
  const int tabs_before = browser()->tab_strip_model()->count();
  const int second_before = second->tab_strip_model()->count();

  browser()->tab_strip_model()->SetTabPinned(0, false);

  EXPECT_TRUE(ShowsPins(second, {}));
  EXPECT_TRUE(base::test::RunUntil(
      [&] { return second->tab_strip_model()->count() == second_before - 1; }));
  EXPECT_EQ(browser()->tab_strip_model()->count(), tabs_before);
  EXPECT_NE(browser()->tab_strip_model()->GetIndexOfWebContents(page),
            TabStripModel::kNoTab);
}

IN_PROC_BROWSER_TEST_F(PinsBrowserTest, SelectingAPinMovesItsLivePageThere) {
  content::WebContents* page = Pin(browser(), GURL(kNotes))->GetContents();
  Browser* second = CreateBrowser(browser()->GetProfile());
  ASSERT_TRUE(ShowsPins(second, {GURL(kNotes)}));
  // The other window has a placeholder with the page's history and no
  // renderer.
  content::WebContents* placeholder = PinPageOf(second);
  EXPECT_NE(placeholder, page);
  EXPECT_FALSE(HasRenderer(placeholder));
  EXPECT_TRUE(
      PinSync::IsPlaceholder(second->tab_strip_model()->GetTabAtIndex(0)));
  ASSERT_TRUE(content::ExecJs(page, kTypeIntoNotes));
  const std::string state = StateOf(page);
  const int ticks = TicksOf(page);

  second->tab_strip_model()->ActivateTabAt(0);

  ASSERT_TRUE(Holds(second, page));
  EXPECT_EQ(StateOf(page), state);
  EXPECT_GE(TicksOf(page), ticks);
  content::WebContents* left = PinPageOf(browser());
  EXPECT_NE(left, page);
  EXPECT_FALSE(HasRenderer(left));
  EXPECT_EQ(left->GetVisibleURL(), GURL(kNotes));

  // Selecting it in the first window brings it back.
  browser()->tab_strip_model()->ActivateTabAt(1);
  browser()->tab_strip_model()->ActivateTabAt(0);
  ASSERT_TRUE(Holds(browser(), page));
  EXPECT_EQ(StateOf(page), state);
  EXPECT_FALSE(HasRenderer(PinPageOf(second)));
}

IN_PROC_BROWSER_TEST_F(PinsBrowserTest, ThePageOutlivesTheWindowThatShowedIt) {
  Browser* second = CreateBrowser(browser()->GetProfile());
  content::WebContents* page = Pin(second, GURL(kNotes))->GetContents();
  ASSERT_TRUE(ShowsPins(browser(), {GURL(kNotes)}));
  ASSERT_TRUE(content::ExecJs(page, kTypeIntoNotes));
  const std::string state = StateOf(page);
  const int ticks = TicksOf(page);
  base::WeakPtr<content::WebContents> alive = page->GetWeakPtr();

  CloseBrowserSynchronously(second);

  ASSERT_TRUE(alive);
  EXPECT_FALSE(HasRenderer(PinPageOf(browser())));
  browser()->tab_strip_model()->ActivateTabAt(0);
  ASSERT_TRUE(Holds(browser(), page));
  EXPECT_EQ(StateOf(page), state);
  EXPECT_GE(TicksOf(page), ticks);
}

IN_PROC_BROWSER_TEST_F(PinsBrowserTest, ANewWindowAfterTheLastOneGetsThePage) {
  Profile* profile = browser()->GetProfile();
  content::WebContents* page = Pin(browser(), GURL(kNotes))->GetContents();
  ASSERT_TRUE(content::ExecJs(page, kTypeIntoNotes));
  const std::string state = StateOf(page);
  base::WeakPtr<content::WebContents> alive = page->GetWeakPtr();

  CloseBrowserSynchronously(browser());

  ASSERT_TRUE(alive);
  Browser* next = CreateBrowser(profile);
  SetBrowser(next);
  ASSERT_TRUE(Holds(next, page));
  EXPECT_EQ(StateOf(page), state);
}

IN_PROC_BROWSER_TEST_F(PinsBrowserTest, MovingAPinMovesItEverywhere) {
  Browser* second = CreateBrowser(browser()->GetProfile());
  Pin(browser(), GURL(kMail));
  Pin(browser(), GURL(kIssues));
  ASSERT_TRUE(ShowsPins(second, {GURL(kMail), GURL(kIssues)}));

  browser()->tab_strip_model()->MoveWebContentsAt(1, 0, false);

  EXPECT_TRUE(ShowsPins(second, {GURL(kIssues), GURL(kMail)}));
}

IN_PROC_BROWSER_TEST_F(PinsBrowserTest, AnotherProfileNeverGetsThem) {
  ProfileManager* manager = g_browser_process->profile_manager();
  Profile& other_profile = profiles::testing::CreateProfileSync(
      manager, manager->GenerateNextProfileDirectoryPath());
  Browser* other = CreateBrowser(&other_profile);
  Pin(browser(), GURL(kMail));
  base::RunLoop().RunUntilIdle();
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(PinnedUrls(other).empty());
  EXPECT_TRUE(PinStore::For(&other_profile)->pins().empty());
}

IN_PROC_BROWSER_TEST_F(PinsBrowserTest, CmdWLeavesAPinOpen) {
  tabs::TabInterface* pin = Pin(browser(), GURL(kMail));
  base::RunLoop().RunUntilIdle();
  TabStripModel* tabs = browser()->tab_strip_model();
  tabs->ActivateTabAt(tabs->GetIndexOfTab(pin));
  const int count = tabs->count();

  chrome::ExecuteCommand(browser(), IDC_CLOSE_TAB);
  base::RunLoop().RunUntilIdle();

  EXPECT_EQ(tabs->count(), count);
  EXPECT_EQ(tabs->GetActiveTab(), pin);
}

IN_PROC_BROWSER_TEST_F(PinsBrowserTest, ResetToHomeLoadsTheHomeUrl) {
  tabs::TabInterface* pin = Pin(browser(), GURL(kIssues));
  base::RunLoop().RunUntilIdle();
  Navigate(pin, GURL(kIssue));

  ExecutePinCommand(pin, kResetToHome);

  EXPECT_TRUE(content::WaitForLoadStop(pin->GetContents()));
  EXPECT_EQ(pin->GetContents()->GetLastCommittedURL(), GURL(kIssues));
}

IN_PROC_BROWSER_TEST_F(PinsBrowserTest, ReplaceHomeKeepsThePage) {
  tabs::TabInterface* pin = Pin(browser(), GURL(kIssues));
  base::RunLoop().RunUntilIdle();
  Navigate(pin, GURL(kIssue));

  ExecutePinCommand(pin, kReplaceHome);

  EXPECT_EQ(PinSync::PinFor(pin)->home, GURL(kIssue));
  EXPECT_EQ(pin->GetContents()->GetLastCommittedURL(), GURL(kIssue));
}

IN_PROC_BROWSER_TEST_F(PinsBrowserTest, EditHomeUrlSavesAndLoads) {
  tabs::TabInterface* pin = Pin(browser(), GURL(kIssues));
  base::RunLoop().RunUntilIdle();
  views::Widget* widget = HomeUrlCard::Show(pin, TileOf(pin));
  ASSERT_TRUE(widget);
  auto* card =
      static_cast<HomeUrlCard*>(widget->widget_delegate()->GetContentsView());
  EXPECT_EQ(card->field()->GetText(), u"chrome://version/?issues");

  card->field()->SetText(u"chrome://version/?inbox");
  widget->widget_delegate()->AsDialogDelegate()->AcceptDialog();

  const GURL inbox("chrome://version/?inbox");
  EXPECT_EQ(PinSync::PinFor(pin)->home, inbox);
  EXPECT_TRUE(content::WaitForLoadStop(pin->GetContents()));
  EXPECT_EQ(pin->GetContents()->GetLastCommittedURL(), inbox);
}

IN_PROC_BROWSER_TEST_F(PinsBrowserTest, APinsMenuHasItsRowsAndNoCloseTab) {
  tabs::TabInterface* pin = Pin(browser(), GURL(kMail));
  base::RunLoop().RunUntilIdle();
  TabStripModel* tabs = browser()->tab_strip_model();
  TabMenuModel menu(nullptr, browser()->GetFeatures().tab_menu_model_delegate(),
                    tabs, tabs->GetIndexOfTab(pin));

  AddPinCommands(&menu, TileOf(pin));

  EXPECT_EQ(menu.GetCommandIdAt(0), kResetToHome);
  EXPECT_EQ(menu.GetLabelAt(0), u"Reset to Home");
  EXPECT_EQ(menu.GetCommandIdAt(1), kReplaceHome);
  EXPECT_EQ(menu.GetCommandIdAt(2), kEditHome);
  EXPECT_FALSE(menu.GetIndexOfCommandId(TabStripModel::CommandCloseTab));
  EXPECT_EQ(menu.GetCommandIdAt(menu.GetItemCount() - 1),
            TabStripModel::CommandTogglePinned);
  EXPECT_TRUE(CanExecutePinCommand(pin));
}

IN_PROC_BROWSER_TEST_F(PinsBrowserTest, ATileKeepsItsPinAndADoubleClickGoesHome) {
  tabs::TabInterface* pin = Pin(browser(), GURL(kIssues));
  base::RunLoop().RunUntilIdle();
  Navigate(pin, GURL(kIssue));
  views::View* tile = TileOf(pin);
  ASSERT_TRUE(tile);
  const gfx::Point center = tile->GetLocalBounds().CenterPoint();
  const int count = browser()->tab_strip_model()->count();

  tile->OnMouseReleased(ui::MouseEvent(
      ui::EventType::kMouseReleased, center, center, ui::EventTimeForNow(),
      ui::EF_MIDDLE_MOUSE_BUTTON, ui::EF_MIDDLE_MOUSE_BUTTON));
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(browser()->tab_strip_model()->count(), count);

  tile->OnMouseReleased(ui::MouseEvent(
      ui::EventType::kMouseReleased, center, center, ui::EventTimeForNow(),
      ui::EF_LEFT_MOUSE_BUTTON | ui::EF_IS_DOUBLE_CLICK,
      ui::EF_LEFT_MOUSE_BUTTON));
  EXPECT_TRUE(content::WaitForLoadStop(pin->GetContents()));
  EXPECT_EQ(pin->GetContents()->GetLastCommittedURL(), GURL(kIssues));
}

IN_PROC_BROWSER_TEST_F(PinsBrowserTest, PRE_PinsComeBackOnceAfterARestart) {
  SessionStartupPref::SetStartupPref(
      browser()->GetProfile(), SessionStartupPref(SessionStartupPref::LAST));
  tabs::TabInterface* pin = Pin(browser(), GURL(kIssues));
  Navigate(pin, GURL(kIssue));
  // Lets the session take the tabs' data before the browser quits.
  base::RunLoop().RunUntilIdle();
}

IN_PROC_BROWSER_TEST_F(PinsBrowserTest, PinsComeBackOnceAfterARestart) {
  // The restored pin keeps the page it was on, and no second copy loads its
  // home URL beside it.
  ASSERT_TRUE(ShowsPins(browser(), {GURL(kIssue)}));
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(PinnedUrls(browser()), std::vector<GURL>{GURL(kIssue)});
  ASSERT_EQ(store()->pins().size(), 1u);
  EXPECT_EQ(store()->pins()[0].home, GURL(kIssues));
}

IN_PROC_BROWSER_TEST_F(PinsBrowserTest,
                       PRE_UnpinningFromTheTabStripAfterARestartAddsNoCopies) {
  PinInThreeWindowsBeforeARestart();
}

IN_PROC_BROWSER_TEST_F(PinsBrowserTest,
                       UnpinningFromTheTabStripAfterARestartAddsNoCopies) {
  UnpinAfterARestart(
      [](TabStripModel* tabs) { tabs->SetTabPinned(0, /*pinned=*/false); });
}

IN_PROC_BROWSER_TEST_F(PinsBrowserTest,
                       PRE_UnpinningFromTheMenuAfterARestartAddsNoCopies) {
  PinInThreeWindowsBeforeARestart();
}

IN_PROC_BROWSER_TEST_F(PinsBrowserTest,
                       UnpinningFromTheMenuAfterARestartAddsNoCopies) {
  UnpinAfterARestart([](TabStripModel* tabs) {
    tabs->ExecuteContextMenuCommand(0, TabStripModel::CommandTogglePinned);
  });
}

}  // namespace
}  // namespace uw
