// Copyright 2026 The uw Authors. All rights reserved.

#include <algorithm>
#include <map>
#include <string>
#include <vector>

#include "base/functional/bind.h"
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
#include "chrome/browser/themes/theme_service.h"
#include "chrome/browser/themes/theme_service_factory.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/browser/ui/browser_window/public/browser_window_features.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface_iterator.h"
#include "chrome/browser/ui/tabs/tab_menu_model.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/browser_widget.h"
#include "chrome/browser/ui/views/tabs/common/tab_view.h"
#include "components/favicon/content/content_favicon_driver.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/events/base_event_utils.h"
#include "ui/events/event.h"
#include "ui/gfx/color_utils.h"
#include "ui/views/controls/textfield/textfield.h"
#include "ui/views/test/widget_test.h"
#include "ui/views/view_utils.h"
#include "ui/views/widget/widget.h"
#include "url/gurl.h"
#include "uw/browser/pins/pin_store.h"
#include "uw/browser/pins/pin_sync.h"
#include "uw/browser/tab_organization/window_screenshot.h"
#include "uw/browser/ui/views/pins/home_url_card.h"
#include "uw/browser/ui/views/pins/inactive_pin_overlay.h"
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
constexpr char kMoreNotes[] = "data:text/html,<title>More notes</title>";
// A page that asks before it is left.
constexpr char kDraft[] =
    "data:text/html,<title>Draft</title><input id=f><script>token="
    "Math.random();onbeforeunload=e=>{e.preventDefault();e.returnValue=''}"
    "</script>";

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

  // Waits until the first pin of `window` has the hazed view over it.
  static views::View* WaitForHaze(Browser* window) {
    views::View* haze = nullptr;
    EXPECT_TRUE(base::test::RunUntil([&] {
      haze = InactivePinOverlay::From(window)->GetShownView();
      return haze != nullptr;
    }));
    return haze;
  }

  static void Click(views::View* view, const gfx::Point& at) {
    view->OnMousePressed(ui::MouseEvent(
        ui::EventType::kMousePressed, at, at, ui::EventTimeForNow(),
        ui::EF_LEFT_MOUSE_BUTTON, ui::EF_LEFT_MOUSE_BUTTON));
    view->OnMouseReleased(ui::MouseEvent(
        ui::EventType::kMouseReleased, at, at, ui::EventTimeForNow(),
        ui::EF_LEFT_MOUSE_BUTTON, ui::EF_LEFT_MOUSE_BUTTON));
  }

  // How many windows have a pin page with a renderer.
  static int LiveCopies() {
    int live = 0;
    for (BrowserWindowInterface* window : GetAllBrowserWindowInterfaces()) {
      TabStripModel* tabs = window->GetTabStripModel();
      for (int i = 0; i < tabs->IndexOfFirstNonPinnedTab(); ++i) {
        live += HasRenderer(tabs->GetWebContentsAt(i));
      }
    }
    return live;
  }

  // Whether a restored page has started to load.
  static bool Started(content::WebContents* page) {
    return page->GetController().GetPendingEntry() ||
           !page->GetPrimaryMainFrame()->GetLastCommittedURL().is_empty();
  }

  // Whether `page`'s tile shows the page's own icon, not the default one.
  static bool ShowsIcon(content::WebContents* page) {
    favicon::FaviconDriver* icons =
        favicon::ContentFaviconDriver::FromWebContents(page);
    return icons && icons->FaviconIsValid() && !icons->GetFavicon().IsEmpty();
  }

  // Moves the live page of the first pin from `from`, where it is selected,
  // to `to`, and returns it.
  content::WebContents* MovePage(Browser* from, Browser* to) {
    content::WebContents* page = PinPageOf(from);
    to->tab_strip_model()->ActivateTabAt(0);
    EXPECT_TRUE(Holds(to, page));
    return page;
  }

  // The window's colors in the pill and in the haze beside it.
  struct HazeColors {
    SkColor pill;
    SkColor haze;
  };
  static HazeColors ColorsOf(Browser* window, views::View* haze) {
    const SkBitmap bitmap = CaptureWindow(window);
    views::View* pill = haze->children().back();
    const gfx::Rect pill_bounds =
        pill->ConvertRectToWidget(pill->GetLocalBounds());
    const gfx::Rect haze_bounds =
        haze->ConvertRectToWidget(haze->GetLocalBounds());
    const float scale = static_cast<float>(bitmap.width()) /
                        haze->GetWidget()->GetRootView()->width();
    const auto at = [&](int x, int y) {
      return bitmap.getColor(x * scale, y * scale);
    };
    // Inside the pill, just past its round end, and in the haze above it.
    return {at(pill_bounds.x() + 24 + 8, pill_bounds.CenterPoint().y()),
            at(haze_bounds.x() + 40, haze_bounds.y() + 40)};
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

// The window can change its theme after the home card closed and before the
// card deletes itself, as when the new home page first paints. The change must
// not reach the gone card.
IN_PROC_BROWSER_TEST_F(PinsBrowserTest,
                       AThemeChangeAfterTheHomeCardClosedIsSafe) {
  tabs::TabInterface* pin = Pin(browser(), GURL(kIssues));
  base::RunLoop().RunUntilIdle();
  views::Widget* card = HomeUrlCard::Show(pin, TileOf(pin));
  ASSERT_TRUE(card);
  views::test::WidgetDestroyedWaiter destroyed(card);
  card->widget_delegate()->AsDialogDelegate()->CancelDialog();
  destroyed.Wait();

  BrowserView::GetBrowserViewForBrowser(browser())
      ->browser_widget()
      ->UserChangedTheme(BrowserThemeChangeType::kWebAppTheme);
  base::RunLoop().RunUntilIdle();
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

IN_PROC_BROWSER_TEST_F(PinsBrowserTest, TheWindowThePageLeftShowsItHazed) {
  Profile* profile = browser()->GetProfile();
  content::WebContents* page = Pin(browser(), GURL(kNotes))->GetContents();
  ASSERT_TRUE(content::ExecJs(page, kTypeIntoNotes));
  const std::string state = StateOf(page);
  Browser* second = CreateBrowser(profile);
  ASSERT_TRUE(ShowsPins(second, {GURL(kNotes)}));
  MovePage(browser(), second);

  views::View* haze = WaitForHaze(browser());
  ASSERT_TRUE(haze);
  EXPECT_FALSE(InactivePinOverlay::From(second)->GetShownView());
  // The pill is the card's white, and the haze over the picture is not.
  ThemeServiceFactory::GetForProfile(profile)->SetBrowserColorScheme(
      ThemeService::BrowserColorScheme::kLight);
  base::RunLoop().RunUntilIdle();
  HazeColors light = ColorsOf(browser(), haze);
  EXPECT_EQ(light.pill, SK_ColorWHITE);
  EXPECT_NE(light.haze, SK_ColorWHITE);
  EXPECT_FALSE(color_utils::IsDark(light.haze));
  SaveWindowScreenshot(browser(), "pin-haze-light");

  ThemeServiceFactory::GetForProfile(profile)->SetBrowserColorScheme(
      ThemeService::BrowserColorScheme::kDark);
  base::RunLoop().RunUntilIdle();
  HazeColors dark = ColorsOf(browser(), haze);
  // The pill takes the dark card's color, and the haze darkens the picture.
  EXPECT_TRUE(color_utils::IsDark(dark.pill));
  EXPECT_LT(color_utils::GetRelativeLuminance(dark.haze),
            color_utils::GetRelativeLuminance(light.haze));
  SaveWindowScreenshot(browser(), "pin-haze-dark");

  // A click anywhere on it brings the page back.
  Click(haze, gfx::Point(40, 40));
  ASSERT_TRUE(Holds(browser(), page));
  EXPECT_FALSE(InactivePinOverlay::From(browser())->GetShownView());
  EXPECT_TRUE(WaitForHaze(second));
  EXPECT_EQ(StateOf(page), state);
}

IN_PROC_BROWSER_TEST_F(PinsBrowserTest,
                       PRE_APlaceholderStaysUnloadedAfterARestart) {
  SessionStartupPref::SetStartupPref(
      browser()->GetProfile(), SessionStartupPref(SessionStartupPref::LAST));
  Pin(browser(), GURL(kMail));
  Browser* second = CreateBrowser(browser()->GetProfile());
  ASSERT_TRUE(ShowsPins(second, {GURL(kMail)}));
  // The first window keeps its placeholder selected.
  MovePage(browser(), second);
  base::RunLoop().RunUntilIdle();
}

IN_PROC_BROWSER_TEST_F(PinsBrowserTest,
                       APlaceholderStaysUnloadedAfterARestart) {
  std::vector<BrowserWindowInterface*> windows;
  ASSERT_TRUE(base::test::RunUntil([&] {
    windows = GetAllBrowserWindowInterfaces();
    return windows.size() == 2u && AllShow(windows, {GURL(kMail)});
  }));
  content::WebContents* live = nullptr;
  // The tab, since its placeholder is remade when the live page changes.
  tabs::TabInterface* placeholder = nullptr;
  for (BrowserWindowInterface* window : windows) {
    tabs::TabInterface* tab = window->GetTabStripModel()->GetTabAtIndex(0);
    if (PinSync::IsPlaceholder(tab)) {
      placeholder = tab;
    } else {
      live = tab->GetContents();
    }
  }
  ASSERT_TRUE(live && placeholder);
  EXPECT_TRUE(base::test::RunUntil([&] { return Started(live); }));
  // The selected placeholder of a shown window never loads a second copy.
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(placeholder->IsActivated());
  EXPECT_TRUE(PinSync::IsPlaceholder(placeholder));
  EXPECT_FALSE(Started(placeholder->GetContents()));
  EXPECT_FALSE(HasRenderer(placeholder->GetContents()));
  ASSERT_EQ(store()->pins().size(), 1u);
}

IN_PROC_BROWSER_TEST_F(PinsBrowserTest,
                       PRE_IconsShowBeforeAHiddenWorkspaceLoads) {
  Profile* profile = browser()->GetProfile();
  SessionStartupPref::SetStartupPref(
      profile, SessionStartupPref(SessionStartupPref::LAST));
  ASSERT_TRUE(embedded_test_server()->Start());
  WorkspaceList& list = WorkspaceList::Get();
  const std::string reading = list.Add(
      "Reading", std::string(list.UnusedIcon()), profile->GetBaseName());
  content::WebContents* page =
      Pin(browser(),
          embedded_test_server()->GetURL("/favicon/page_with_favicon.html"))
          ->GetContents();
  ASSERT_TRUE(base::test::RunUntil([&] { return ShowsIcon(page); }));
  // The live page stays in the window that goes behind.
  Browser* partner = nullptr;
  WorkspaceWindow::From(browser())->Switch(
      reading, base::BindOnce(
                   [](Browser** partner, BrowserWindowInterface* shown) {
                     *partner = shown->GetBrowserForMigrationOnly();
                   },
                   &partner));
  ASSERT_TRUE(base::test::RunUntil([&] {
    return partner && partner->tab_strip_model()->IndexOfFirstNonPinnedTab();
  }));
  base::RunLoop().RunUntilIdle();
}

IN_PROC_BROWSER_TEST_F(PinsBrowserTest, IconsShowBeforeAHiddenWorkspaceLoads) {
  BrowserWindowInterface* hidden = nullptr;
  BrowserWindowInterface* shown = nullptr;
  ASSERT_TRUE(base::test::RunUntil([&] {
    hidden = shown = nullptr;
    for (BrowserWindowInterface* window : GetAllBrowserWindowInterfaces()) {
      if (window->GetTabStripModel()->IndexOfFirstNonPinnedTab() == 1) {
        (WorkspaceWindow::IsHidden(window) ? hidden : shown) = window;
      }
    }
    return hidden && shown;
  }));
  content::WebContents* live = hidden->GetTabStripModel()->GetWebContentsAt(0);
  ASSERT_FALSE(
      PinSync::IsPlaceholder(hidden->GetTabStripModel()->GetTabAtIndex(0)));

  // The hidden window's page shows its saved icon without loading, and the
  // shown window's placeholder takes the icon from it.
  EXPECT_TRUE(base::test::RunUntil([&] { return ShowsIcon(live); }));
  EXPECT_TRUE(base::test::RunUntil([&] {
    return ShowsIcon(shown->GetTabStripModel()->GetWebContentsAt(0));
  }));
  EXPECT_FALSE(Started(live));
}

IN_PROC_BROWSER_TEST_F(PinsBrowserTest, ThePageFollowsAWorkspaceSwitch) {
  Profile* profile = browser()->GetProfile();
  WorkspaceList& list = WorkspaceList::Get();
  const std::string personal = list.workspaces()[0].id;
  const std::string reading = list.Add(
      "Reading", std::string(list.UnusedIcon()), profile->GetBaseName());
  content::WebContents* page = Pin(browser(), GURL(kNotes))->GetContents();
  ASSERT_TRUE(content::ExecJs(page, kTypeIntoNotes));
  const std::string state = StateOf(page);
  Browser* partner = nullptr;
  WorkspaceWindow::From(browser())->Switch(
      reading, base::BindOnce(
                   [](Browser** partner, BrowserWindowInterface* shown) {
                     *partner = shown->GetBrowserForMigrationOnly();
                   },
                   &partner));
  ASSERT_TRUE(base::test::RunUntil([&] { return partner != nullptr; }));
  ASSERT_TRUE(ShowsPins(partner, {GURL(kNotes)}));
  // Selecting the pin in the shown window takes the page from the hidden one.
  MovePage(browser(), partner);

  // Switching back, the page follows into the window that shows.
  WorkspaceWindow::From(partner)->Switch(personal);
  ASSERT_TRUE(Holds(browser(), page));
  EXPECT_EQ(StateOf(page), state);
  EXPECT_EQ(LiveCopies(), 1);
}

IN_PROC_BROWSER_TEST_F(PinsBrowserTest, ReloadInAPlaceholderBringsThePage) {
  content::WebContents* page = Pin(browser(), GURL(kNotes))->GetContents();
  Browser* second = CreateBrowser(browser()->GetProfile());
  ASSERT_TRUE(ShowsPins(second, {GURL(kNotes)}));
  MovePage(browser(), second);
  const std::string state = StateOf(page);

  chrome::ExecuteCommand(browser(), IDC_RELOAD);

  ASSERT_TRUE(Holds(browser(), page));
  EXPECT_TRUE(content::WaitForLoadStop(page));
  // The live page reloaded: the same page, loaded again.
  EXPECT_NE(StateOf(page), state);
  EXPECT_EQ(LiveCopies(), 1);
}

IN_PROC_BROWSER_TEST_F(PinsBrowserTest, BackInAPlaceholderGoesBackInThePage) {
  content::WebContents* page = Pin(browser(), GURL(kNotes))->GetContents();
  ASSERT_TRUE(content::NavigateToURL(page, GURL(kMoreNotes)));
  Browser* second = CreateBrowser(browser()->GetProfile());
  ASSERT_TRUE(ShowsPins(second, {GURL(kMoreNotes)}));
  MovePage(browser(), second);

  chrome::ExecuteCommand(browser(), IDC_BACK);

  ASSERT_TRUE(Holds(browser(), page));
  EXPECT_TRUE(base::test::RunUntil(
      [&] { return page->GetLastCommittedURL() == GURL(kNotes); }));
  EXPECT_EQ(LiveCopies(), 1);
}

IN_PROC_BROWSER_TEST_F(PinsBrowserTest, PlaceholdersFollowThePagesHistory) {
  content::WebContents* page = Pin(browser(), GURL(kNotes))->GetContents();
  Browser* second = CreateBrowser(browser()->GetProfile());
  ASSERT_TRUE(ShowsPins(second, {GURL(kNotes)}));

  ASSERT_TRUE(content::NavigateToURL(page, GURL(kMoreNotes)));

  EXPECT_TRUE(ShowsPins(second, {GURL(kMoreNotes)}));
  EXPECT_TRUE(base::test::RunUntil(
      [&] { return PinPageOf(second)->GetTitle() == u"More notes"; }));
  EXPECT_EQ(PinPageOf(second)->GetController().GetEntryCount(), 2);
  EXPECT_FALSE(HasRenderer(PinPageOf(second)));
}

IN_PROC_BROWSER_TEST_F(PinsBrowserTest,
                       UnpinningWhenNoWindowHoldsThePageOpensItHere) {
  Browser* second = CreateBrowser(browser()->GetProfile());
  content::WebContents* page = Pin(second, GURL(kNotes))->GetContents();
  ASSERT_TRUE(ShowsPins(browser(), {GURL(kNotes)}));
  ASSERT_TRUE(content::ExecJs(page, kTypeIntoNotes));
  const std::string state = StateOf(page);
  CloseBrowserSynchronously(second);

  browser()->tab_strip_model()->SetTabPinned(0, /*pinned=*/false);

  TabStripModel* tabs = browser()->tab_strip_model();
  ASSERT_TRUE(base::test::RunUntil([&] {
    return tabs->GetIndexOfWebContents(page) != TabStripModel::kNoTab;
  }));
  EXPECT_FALSE(tabs->IsTabPinned(tabs->GetIndexOfWebContents(page)));
  EXPECT_EQ(StateOf(page), state);
  EXPECT_TRUE(store()->pins().empty());
}

IN_PROC_BROWSER_TEST_F(PinsBrowserTest,
                       APageThatAsksBeforeLeavingLetsItsWindowClose) {
  Browser* second = CreateBrowser(browser()->GetProfile());
  content::WebContents* page = Pin(second, GURL(kDraft))->GetContents();
  ASSERT_TRUE(ShowsPins(browser(), {GURL(kDraft)}));
  // Gives the page the user activation it needs to ask.
  ASSERT_TRUE(content::ExecJs(page, kTypeIntoNotes));
  base::WeakPtr<content::WebContents> alive = page->GetWeakPtr();

  CloseBrowserSynchronously(second);

  ASSERT_TRUE(alive);
  browser()->tab_strip_model()->ActivateTabAt(0);
  ASSERT_TRUE(Holds(browser(), page));
  // Quitting still asks, as Chromium does, so the test lets it go.
  EXPECT_TRUE(content::ExecJs(page, "onbeforeunload = null"));
}

IN_PROC_BROWSER_TEST_F(PinsBrowserTest, TwoQuickSelectionsLeaveOneLivePage) {
  content::WebContents* page = Pin(browser(), GURL(kNotes))->GetContents();
  ASSERT_TRUE(content::ExecJs(page, kTypeIntoNotes));
  const std::string state = StateOf(page);
  Browser* second = CreateBrowser(browser()->GetProfile());
  Browser* third = CreateBrowser(browser()->GetProfile());
  ASSERT_TRUE(ShowsPins(second, {GURL(kNotes)}));
  ASSERT_TRUE(ShowsPins(third, {GURL(kNotes)}));

  second->tab_strip_model()->ActivateTabAt(0);
  third->tab_strip_model()->ActivateTabAt(0);

  ASSERT_TRUE(Holds(third, page));
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(PinPageOf(third), page);
  EXPECT_EQ(LiveCopies(), 1);
  EXPECT_EQ(StateOf(page), state);
}

IN_PROC_BROWSER_TEST_F(PinsBrowserTest,
                       CmdShiftTBringsTheWindowBackWithItsPage) {
  Browser* second = CreateBrowser(browser()->GetProfile());
  content::WebContents* page = Pin(second, GURL(kNotes))->GetContents();
  ASSERT_TRUE(ShowsPins(browser(), {GURL(kNotes)}));
  ASSERT_TRUE(content::ExecJs(page, kTypeIntoNotes));
  const std::string state = StateOf(page);
  CloseBrowserSynchronously(second);

  chrome::RestoreTab(browser());

  Browser* back = nullptr;
  ASSERT_TRUE(base::test::RunUntil([&] {
    for (BrowserWindowInterface* window : GetAllBrowserWindowInterfaces()) {
      if (window != browser()) {
        back = window->GetBrowserForMigrationOnly();
      }
    }
    return back && back->tab_strip_model()->count() > 0 &&
           PinPageOf(back) == page;
  }));
  EXPECT_EQ(StateOf(page), state);
  // It comes back as the same pin, not a new one.
  EXPECT_EQ(store()->pins().size(), 1u);
  EXPECT_EQ(PinnedUrls(browser()), std::vector<GURL>{GURL(kNotes)});
}

// Needs a real window: run it with `pnpm run window-tests`.
IN_PROC_BROWSER_TEST_F(PinsBrowserTest, MANUAL_TheHazedViewShowsInARealWindow) {
  Profile* profile = browser()->GetProfile();
  ThemeServiceFactory::GetForProfile(profile)->SetBrowserColorScheme(
      ThemeService::BrowserColorScheme::kLight);
  Pin(browser(), GURL(kNotes));
  Browser* second = CreateBrowser(profile);
  ASSERT_TRUE(ShowsPins(second, {GURL(kNotes)}));
  MovePage(browser(), second);
  views::View* haze = WaitForHaze(browser());
  ASSERT_TRUE(haze);
  // macOS draws a window behind another only once it comes to the front.
  browser()->GetWindow()->Activate();

  views::View* pill = haze->children().back();
  SkColor in_pill = SK_ColorTRANSPARENT;
  SkColor in_haze = SK_ColorTRANSPARENT;
  ASSERT_TRUE(base::test::RunUntil([&] {
    const SkBitmap bitmap = CaptureNativeWindow(browser());
    const gfx::Rect pill_bounds =
        pill->ConvertRectToWidget(pill->GetLocalBounds());
    const gfx::Rect haze_bounds =
        haze->ConvertRectToWidget(haze->GetLocalBounds());
    const float scale = static_cast<float>(bitmap.width()) /
                        haze->GetWidget()->GetWindowBoundsInScreen().width();
    in_pill = bitmap.getColor((pill_bounds.x() + 24 + 8) * scale,
                              pill_bounds.CenterPoint().y() * scale);
    in_haze = bitmap.getColor((haze_bounds.x() + 40) * scale,
                              (haze_bounds.y() + 40) * scale);
    return in_haze != SK_ColorWHITE;
  }));
  EXPECT_EQ(in_pill, SK_ColorWHITE);
  EXPECT_FALSE(color_utils::IsDark(in_haze));
  SaveNativeWindowScreenshot(browser(), "pin-haze-real-window");
}

}  // namespace
}  // namespace uw
