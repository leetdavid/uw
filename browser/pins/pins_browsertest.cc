// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include <string>
#include <vector>

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

namespace uw {
namespace {

constexpr char kMail[] = "chrome://version/?mail";
constexpr char kIssues[] = "chrome://version/?issues";
constexpr char kIssue[] = "chrome://version/?issue-412";

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
  Pin(browser(), GURL(kMail));
  ASSERT_TRUE(ShowsPins(second, {GURL(kMail)}));
  const int tabs_before = second->tab_strip_model()->count();

  second->tab_strip_model()->SetTabPinned(0, false);

  EXPECT_TRUE(ShowsPins(browser(), {}));
  EXPECT_TRUE(ShowsPins(second, {}));
  // The page stays open as a regular tab in the window that unpinned it.
  EXPECT_EQ(second->tab_strip_model()->count(), tabs_before);
  EXPECT_TRUE(store()->pins().empty());
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

}  // namespace
}  // namespace uw
