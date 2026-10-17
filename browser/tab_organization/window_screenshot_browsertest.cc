// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/browser/tab_organization/window_screenshot.h"

#include <string>

#include "base/run_loop.h"
#include "base/test/run_until.h"
#include "base/time/time.h"
#include "chrome/app/chrome_command_ids.h"
#include "chrome/browser/headless/test/headless_mode_browsertest.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/themes/theme_service.h"
#include "chrome/browser/themes/theme_service_factory.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_element_identifiers.h"
#include "chrome/browser/ui/navigator/browser_navigator.h"
#include "chrome/browser/ui/navigator/browser_navigator_params.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/tabs/vertical_tab_strip_state_controller.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "components/prefs/pref_service.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "ui/events/base_event_utils.h"
#include "ui/events/event.h"
#include "ui/views/controls/button/image_button.h"
#include "ui/views/interaction/element_tracker_views.h"
#include "ui/views/test/button_test_api.h"
#include "url/gurl.h"
#include "uw/browser/tab_organization/ai_connection.h"
#include "uw/browser/tab_organization/tab_organization_controller.h"
#include "uw/browser/ui/views/tab_organization/new_button.h"
#include "uw/browser/ui/views/tab_organization/organize_bar.h"
#include "uw/browser/ui/views/tab_organization/organize_card.h"
#include "uw/components/ai_providers/providers.h"

namespace uw {
namespace {

// Stages the sidebar with nested tabs and one task for a screenshot.
class WindowScreenshotBrowserTest : public headless::HeadlessModeBrowserTest {
 protected:
  tabs::TabInterface* OpenLinkFrom(tabs::TabInterface* source,
                                   const std::string& title) {
    NavigateParams params(browser(),
                          GURL("data:text/html,<title>" + title + "</title>"),
                          ui::PAGE_TRANSITION_LINK);
    params.disposition = WindowOpenDisposition::NEW_BACKGROUND_TAB;
    params.source_contents = source->GetContents();
    Navigate(&params);
    EXPECT_TRUE(
        content::WaitForLoadStop(params.navigated_or_inserted_contents));
    base::RunLoop().RunUntilIdle();
    return browser()->tab_strip_model()->GetTabForWebContents(
        params.navigated_or_inserted_contents);
  }

  // Pins four favorites, opens nested tabs, and files two of them into a
  // task.
  void StageSidebar() {
    TabOrganizationController* controller =
        TabOrganizationController::From(browser());
    TabStripModel* tabs = browser()->tab_strip_model();
    tabs::TabInterface* first = tabs->GetTabAtIndex(0);
    for (const char* favorite : {"Mail", "Issues", "Code", "Music"}) {
      tabs->SetTabPinned(tabs->GetIndexOfTab(OpenLinkFrom(first, favorite)),
                         true);
    }
    base::RunLoop().RunUntilIdle();
    tabs::TabInterface* flights = OpenLinkFrom(first, "SFO to LIS flights");
    OpenLinkFrom(flights, "Fare details: TAP Air Portugal");
    tabs::TabInterface* news = OpenLinkFrom(first, "Hacker News");
    OpenLinkFrom(news, "Show HN: Tree-style tabs for Chromium");
    ASSERT_TRUE(controller->MoveToNewTask({*controller->IdFor(flights)},
                                          u"Plan Lisbon trip"));
    EXPECT_EQ(controller->tree().Tasks().size(), 1u);
  }

  OrganizeBar* bar() {
    return FindBar(BrowserView::GetBrowserViewForBrowser(browser()));
  }

  static OrganizeBar* FindBar(views::View* view) {
    if (OrganizeBar* bar = views::AsViewClass<OrganizeBar>(view)) {
      return bar;
    }
    for (views::View* child : view->children()) {
      if (OrganizeBar* bar = FindBar(child)) {
        return bar;
      }
    }
    return nullptr;
  }

  // Opens the Organize card and waits for its rows.
  OrganizeCard* OpenOrganizeCard() {
    bar()->ShowMenu();
    views::Widget* widget = bar()->organize_card();
    return widget ? views::AsViewClass<OrganizeCard>(
                        widget->widget_delegate()->GetContentsView())
                  : nullptr;
  }

  views::Button* Row(OrganizeCard* card, int id) {
    base::RunLoop().RunUntilIdle();
    return views::AsViewClass<views::Button>(card->GetViewByID(id));
  }

  void SetUpAModel() {
    PrefService* prefs = browser()->GetProfile()->GetPrefs();
    SetChoice(*prefs, ModelSlot::kLanguage,
              AddProvider(*prefs, PresetProvider(ProviderPreset::kOllama)),
              "llama3.2");
  }

  static void Click(views::Button* button) {
    views::test::ButtonTestApi(button).NotifyClick(
        ui::MouseEvent(ui::EventType::kMousePressed, gfx::Point(), gfx::Point(),
                       ui::EventTimeForNow(), ui::EF_LEFT_MOUSE_BUTTON, 0));
  }
};

IN_PROC_BROWSER_TEST_F(WindowScreenshotBrowserTest, Sidebar) {
  StageSidebar();
  SaveWindowScreenshot(browser(), "sidebar");
}

IN_PROC_BROWSER_TEST_F(WindowScreenshotBrowserTest, SidebarInDarkMode) {
  ThemeServiceFactory::GetForProfile(browser()->GetProfile())
      ->SetBrowserColorScheme(ThemeService::BrowserColorScheme::kDark);
  StageSidebar();
  SaveWindowScreenshot(browser(), "sidebar-dark");
}

// The new button at the right end of the foot replaces Chromium's "+" under
// the tab list. "+" opens a tab as Cmd+T does, and its menu lists what can
// be started, each with its key. The narrow sidebar keeps a lone "+".
IN_PROC_BROWSER_TEST_F(WindowScreenshotBrowserTest,
                       TheNewButtonSitsAtTheFootAndOpensATab) {
  browser()->GetProfile()->GetPrefs()->SetBoolean(kCommandBarEnabledPref,
                                                  false);
  StageSidebar();
  ASSERT_TRUE(bar());
  NewButton* button = bar()->new_button();
  ASSERT_TRUE(button && button->GetVisible());
  EXPECT_TRUE(button->caret()->GetVisible());
  for (const views::View* tab_list_plus :
       views::ElementTrackerViews::GetInstance()->GetAllMatchingViews(
           kNewTabButtonElementId,
           views::ElementTrackerViews::GetContextForView(button))) {
    EXPECT_FALSE(tab_list_plus->IsDrawn());
  }
  SaveWindowScreenshot(browser(), "new-button");

  TabStripModel* tabs = browser()->tab_strip_model();
  const int count = tabs->count();
  const size_t tasks =
      TabOrganizationController::From(browser())->tree().Tasks().size();
  Click(button->plus());
  ASSERT_TRUE(base::test::RunUntil([&] { return tabs->count() == count + 1; }));
  EXPECT_EQ(TabOrganizationController::From(browser())->tree().Tasks().size(),
            tasks);

  NewItemsMenu menu(browser());
  ASSERT_EQ(menu.GetItemCount(), 6u);
  EXPECT_EQ(menu.GetCommandIdAt(0), IDC_NEW_TAB);
  EXPECT_EQ(menu.GetCommandIdAt(1), IDC_CREATE_NEW_TAB_GROUP);
  EXPECT_EQ(menu.GetCommandIdAt(5), IDC_NEW_SPLIT_TAB);
  ui::Accelerator key;
  ASSERT_TRUE(menu.GetAcceleratorForCommandId(IDC_CREATE_NEW_TAB_GROUP, &key));
  EXPECT_EQ(key, ui::Accelerator(ui::VKEY_N,
                                 ui::EF_CONTROL_DOWN | ui::EF_COMMAND_DOWN));

  tabs::VerticalTabStripStateController* state =
      tabs::VerticalTabStripStateController::From(browser());
  state->RequestCollapse(true);
  ASSERT_TRUE(base::test::RunUntil([&] { return state->IsCollapsed(); }));
  EXPECT_TRUE(bar()->GetVisible());
  EXPECT_TRUE(button->plus()->GetVisible());
  EXPECT_FALSE(button->caret()->GetVisible());
  SaveWindowScreenshot(browser(), "new-button-narrow");
}

// The Organize card says what organization is doing and offers the one step
// that applies: Turn On, Set Up, the three pause lengths, or Resume.
IN_PROC_BROWSER_TEST_F(WindowScreenshotBrowserTest,
                       TheOrganizeCardSaysWhatOrganizationIsDoing) {
  StageSidebar();
  PrefService* prefs = browser()->GetProfile()->GetPrefs();
  prefs->SetBoolean(kOrganizationEnabledPref, false);
  OrganizeCard* card = OpenOrganizeCard();
  ASSERT_TRUE(card);
  EXPECT_EQ(card->StatusForTesting(), u"Off");
  Click(Row(card, OrganizeCard::kTurnOnId));
  EXPECT_TRUE(prefs->GetBoolean(kOrganizationEnabledPref));
  ASSERT_TRUE(Row(card, OrganizeCard::kSetUpId)) << card->StatusForTesting();
  EXPECT_EQ(card->StatusForTesting(), u"Needs a model");

  SetUpAModel();
  bar()->organize_card()->Close();
  ASSERT_TRUE(base::test::RunUntil([&] { return !bar()->organize_card(); }));
  card = OpenOrganizeCard();
  EXPECT_EQ(card->StatusForTesting(), u"Organizing");
  SaveWidgetScreenshot(bar()->organize_card(), "organize-card-organizing");
  Click(Row(card, OrganizeCard::kPauseHourId));
  ASSERT_TRUE(Row(card, OrganizeCard::kResumeId));
  EXPECT_EQ(card->StatusForTesting(), u"Paused, resuming in 1 hour");
  EXPECT_EQ(ReadPauseState(prefs), PauseMode::kTimed);
  SaveWidgetScreenshot(bar()->organize_card(), "organize-card");

  SetPauseState(prefs, PauseMode::kTimed, base::Minutes(42));
  Row(card, OrganizeCard::kResumeId);
  EXPECT_EQ(card->StatusForTesting(), u"Paused, resuming in 42 min");
  Click(Row(card, OrganizeCard::kResumeId));
  EXPECT_EQ(ReadPauseState(prefs), PauseMode::kOff);
  Click(Row(card, OrganizeCard::kPauseUntilRestartId));
  Row(card, OrganizeCard::kResumeId);
  EXPECT_EQ(card->StatusForTesting(), u"Paused until uw restarts");
  Click(Row(card, OrganizeCard::kResumeId));
  Click(Row(card, OrganizeCard::kPauseUntilResumedId));
  Row(card, OrganizeCard::kResumeId);
  EXPECT_EQ(card->StatusForTesting(), u"Paused");
  Click(Row(card, OrganizeCard::kResumeId));
  EXPECT_EQ(ReadPauseState(prefs), PauseMode::kOff);

  const bool activity = prefs->GetBoolean(kOrganizationShowActivityPref);
  Click(Row(card, OrganizeCard::kActivityId));
  EXPECT_NE(prefs->GetBoolean(kOrganizationShowActivityPref), activity);
}

// Undo Move names the move it reverses and closes the card. With nothing
// left to undo, the row is grayed.
IN_PROC_BROWSER_TEST_F(WindowScreenshotBrowserTest,
                       UndoMoveNamesTheMoveItReverses) {
  StageSidebar();
  OrganizeCard* card = OpenOrganizeCard();
  ASSERT_TRUE(card);
  views::Button* undo = Row(card, OrganizeCard::kUndoId);
  ASSERT_TRUE(undo && undo->GetEnabled());
  EXPECT_EQ(undo->GetViewAccessibility().GetCachedDescription(),
            u"SFO to LIS flights \u2192 Plan Lisbon trip");
  Click(undo);
  ASSERT_TRUE(base::test::RunUntil([&] { return !bar()->organize_card(); }));
  EXPECT_TRUE(
      TabOrganizationController::From(browser())->tree().Tasks().empty());

  while (TabOrganizationController::From(browser())->CanUndo()) {
    TabOrganizationController::From(browser())->Undo();
  }
  card = OpenOrganizeCard();
  EXPECT_FALSE(Row(card, OrganizeCard::kUndoId)->GetEnabled());
}

IN_PROC_BROWSER_TEST_F(WindowScreenshotBrowserTest, TheOrganizeCardInDarkMode) {
  ThemeServiceFactory::GetForProfile(browser()->GetProfile())
      ->SetBrowserColorScheme(ThemeService::BrowserColorScheme::kDark);
  StageSidebar();
  SetUpAModel();
  PrefService* prefs = browser()->GetProfile()->GetPrefs();
  prefs->SetBoolean(kOrganizationEnabledPref, true);
  SetPauseState(prefs, PauseMode::kTimed, base::Hours(1));
  OpenOrganizeCard();
  base::RunLoop().RunUntilIdle();
  SaveWidgetScreenshot(bar()->organize_card(), "organize-card-dark");
}

}  // namespace
}  // namespace uw
