// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/browser/tab_organization/window_screenshot.h"

#include <optional>
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
#include "chrome/browser/ui/color/chrome_color_id.h"
#include "chrome/browser/ui/navigator/browser_navigator.h"
#include "chrome/browser/ui/navigator/browser_navigator_params.h"
#include "chrome/browser/ui/tabs/tab_group_model.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/tabs/vertical_tab_strip_state_controller.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/multi_contents_view.h"
#include "chrome/browser/ui/views/frame/top_container_view.h"
#include "chrome/browser/ui/views/frame/vertical_tab_strip_region_view.h"
#include "chrome/browser/ui/views/tabs/common/tab_group_header_view.h"
#include "chrome/browser/ui/views/toolbar/toolbar_view.h"
#include "components/prefs/pref_service.h"
#include "components/tab_groups/tab_group_color.h"
#include "components/tab_groups/tab_group_id.h"
#include "components/tabs/public/tab_group.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/events/base_event_utils.h"
#include "ui/events/event.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/color/color_provider.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/views/controls/button/image_button.h"
#include "ui/views/interaction/element_tracker_views.h"
#include "ui/views/test/button_test_api.h"
#include "ui/views/view_utils.h"
#include "ui/views/widget/widget.h"
#include "url/gurl.h"
#include "uw/browser/tab_organization/ai_connection.h"
#include "uw/browser/tab_organization/commands.h"
#include "uw/browser/tab_organization/tab_organization_controller.h"
#include "uw/browser/ui/views/tab_organization/new_button.h"
#include "uw/browser/ui/views/tab_organization/organize_bar.h"
#include "uw/browser/ui/views/tab_organization/organize_card.h"
#include "uw/browser/ui/views/tab_organization/task_card.h"
#include "uw/browser/ui/views/tab_organization/task_name_field.h"
#include "uw/browser/ui/views/tab_organization/window_commands.h"
#include "uw/browser/workspaces/workspace_commands.h"
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

  // The task StageSidebar filed.
  tab_groups::TabGroupId Task() {
    return browser()->tab_strip_model()->group_model()->ListTabGroups().front();
  }

  TabGroupHeaderView* RowOf(const tab_groups::TabGroupId& task) {
    return FindRow(BrowserView::GetBrowserViewForBrowser(browser()), task);
  }

  // Looks for the row by its name field, which keeps its task's id after a
  // closed task's row loses its group.
  static TabGroupHeaderView* FindRow(views::View* view,
                                     const tab_groups::TabGroupId& task) {
    if (TaskNameField* field = views::AsViewClass<TaskNameField>(view);
        field && field->group() == task) {
      return views::AsViewClass<TabGroupHeaderView>(field->parent());
    }
    for (views::View* child : view->children()) {
      if (TabGroupHeaderView* row = FindRow(child, task)) {
        return row;
      }
    }
    return nullptr;
  }

  // The open task card, or null.
  views::Widget* TaskCardWidget() {
    for (views::Widget* widget : views::Widget::GetAllOwnedWidgets(
             BrowserView::GetBrowserViewForBrowser(browser())
                 ->GetWidget()
                 ->GetNativeView())) {
      if (!widget->IsClosed() && widget->widget_delegate() &&
          views::IsViewClass<TaskCard>(
              widget->widget_delegate()->GetContentsView())) {
        return widget;
      }
    }
    return nullptr;
  }

  // Opens the card of the task with a right-click on its row.
  views::Widget* RightClick(TabGroupHeaderView* row) {
    Release(row, ui::EF_RIGHT_MOUSE_BUTTON);
    return TaskCardWidget();
  }

  static views::Button* CardRow(views::Widget* card, TaskCard::Id id) {
    return views::AsViewClass<views::Button>(
        card->widget_delegate()->GetContentsView()->GetViewByID(id));
  }

  // Ends a click on `row` with the button in `flags`, the way the row
  // takes clicks.
  static void Release(TabGroupHeaderView* row, int flags) {
    const int button =
        flags & (ui::EF_LEFT_MOUSE_BUTTON | ui::EF_RIGHT_MOUSE_BUTTON);
    row->OnMouseReleased(ui::MouseEvent(ui::EventType::kMouseReleased,
                                        gfx::Point(), gfx::Point(),
                                        ui::EventTimeForNow(), flags, button));
  }

  static void Type(TaskNameField* field, ui::KeyboardCode key) {
    field->HandleKeyEvent(
        field, ui::KeyEvent(ui::EventType::kKeyPressed, key, ui::EF_NONE));
  }

  std::u16string TitleOf(const tab_groups::TabGroupId& task) {
    return std::u16string(browser()
                              ->tab_strip_model()
                              ->group_model()
                              ->GetTabGroup(task)
                              ->visual_data()
                              ->title());
  }

  static void Click(views::Button* button) {
    views::test::ButtonTestApi(button).NotifyClick(
        ui::MouseEvent(ui::EventType::kMousePressed, gfx::Point(), gfx::Point(),
                       ui::EventTimeForNow(), ui::EF_LEFT_MOUSE_BUTTON, 0));
  }

  // Collapses the sidebar and checks the page card: it starts level with the
  // narrow sidebar, below the window's close, minimize, and zoom buttons, and
  // its toolbar and page share a left edge. Nothing in the toolbar's color
  // covers the buttons, so the tint runs behind them.
  void CheckTheNarrowSidebar(const std::string& screenshot) {
    tabs::VerticalTabStripStateController* state =
        tabs::VerticalTabStripStateController::From(browser());
    state->RequestCollapse(true);
    ASSERT_TRUE(base::test::RunUntil([&] { return state->IsCollapsed(); }));
    const SkBitmap pixels = CaptureWindow(browser());
    BrowserView* view = BrowserView::GetBrowserViewForBrowser(browser());
    const gfx::Rect rail =
        view->vertical_tab_strip_region_view_for_testing()->bounds();
    const gfx::Rect toolbar = views::View::ConvertRectToTarget(
        view->toolbar()->parent(), view, view->toolbar()->bounds());
    const gfx::Rect page = views::View::ConvertRectToTarget(
        view->multi_contents_view()->parent(), view,
        view->multi_contents_view()->bounds());
    EXPECT_EQ(toolbar.x(), page.x());
    EXPECT_EQ(toolbar.y(), rail.y());
    EXPECT_EQ(view->top_container()->x(), page.x());

    const float scale = static_cast<float>(pixels.width()) / view->width();
    const SkColor above_card =
        pixels.getColor(static_cast<int>((toolbar.x() + 40) * scale),
                        static_cast<int>((toolbar.y() - 3) * scale));
    EXPECT_NE(above_card, view->GetColorProvider()->GetColor(kColorToolbar));
    SaveWindowScreenshot(browser(), screenshot);
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

IN_PROC_BROWSER_TEST_F(WindowScreenshotBrowserTest,
                       TheNarrowSidebarLeavesTheWindowButtonsOnTheTint) {
  StageSidebar();
  CheckTheNarrowSidebar("sidebar-collapsed");
}

// Run with pnpm run window-tests. macOS draws the buttons only in a real
// window.
IN_PROC_BROWSER_TEST_F(WindowScreenshotBrowserTest,
                       MANUAL_TheNarrowSidebarInARealWindow) {
  StageSidebar();
  CheckTheNarrowSidebar("sidebar-collapsed-real-window");
  SaveNativeWindowScreenshot(browser(), "sidebar-collapsed-native");
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
  ASSERT_EQ(menu.GetItemCount(), 7u);
  EXPECT_EQ(menu.GetCommandIdAt(0), IDC_NEW_TAB);
  EXPECT_EQ(menu.GetCommandIdAt(1), IDC_CREATE_NEW_TAB_GROUP);
  EXPECT_EQ(menu.GetCommandIdAt(5), IDC_NEW_SPLIT_TAB);
  EXPECT_EQ(menu.GetCommandIdAt(6), kNewWorkspaceCommand);
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

// The task menu is a card under the task's row, opened by a right-click and
// by the row's menu button alike. It offers Name Task only with a language
// model to ask.
IN_PROC_BROWSER_TEST_F(WindowScreenshotBrowserTest,
                       TheTaskMenuIsACardUnderTheTaskRow) {
  StageSidebar();
  TabGroupHeaderView* row = RowOf(Task());
  ASSERT_TRUE(row);
  views::Widget* card = RightClick(row);
  ASSERT_TRUE(card);
  EXPECT_FALSE(CardRow(card, TaskCard::kNameId));
  card->Close();
  ASSERT_TRUE(base::test::RunUntil([&] { return !TaskCardWidget(); }));

  SetUpAModel();
  Click(row->editor_bubble_button());
  card = TaskCardWidget();
  ASSERT_TRUE(card);
  for (TaskCard::Id id :
       {TaskCard::kRenameId, TaskCard::kNameId, TaskCard::kNewTabId,
        TaskCard::kNewWindowId, TaskCard::kScratchpadId, TaskCard::kCloseId}) {
    ASSERT_TRUE(CardRow(card, id)) << id;
    EXPECT_TRUE(CardRow(card, id)->GetEnabled()) << id;
  }
  EXPECT_EQ(card->widget_delegate()
                ->GetContentsView()
                ->GetViewByID(TaskCard::kColorsId)
                ->children()
                .size(),
            tab_groups::GetTabGroupColorLabelMap().size());
  EXPECT_GE(card->GetWindowBoundsInScreen().bottom(),
            row->GetBoundsInScreen().bottom());
  base::RunLoop().RunUntilIdle();
  SaveWidgetScreenshot(card, "task-card");
}

// A color dot recolors the task. New Tab in Task opens a tab at its end, and
// Move Tabs to Scratchpad takes its tabs out in one move that Undo reverses.
// Each closes the card. Close Task closes the task's tabs.
IN_PROC_BROWSER_TEST_F(WindowScreenshotBrowserTest,
                       TheTaskCardRecolorsMovesAndClosesTheTask) {
  StageSidebar();
  TabStripModel* tabs = browser()->tab_strip_model();
  TabOrganizationController* controller =
      TabOrganizationController::From(browser());
  const tab_groups::TabGroupId task = Task();

  views::Widget* card = RightClick(RowOf(task));
  views::View* colors = card->widget_delegate()->GetContentsView()->GetViewByID(
      TaskCard::kColorsId);
  ASSERT_TRUE(colors);
  const auto& [color, label] = *tab_groups::GetTabGroupColorLabelMap().rbegin();
  ASSERT_NE(tabs->group_model()->GetTabGroup(task)->visual_data()->color(),
            color);
  Click(views::AsViewClass<views::Button>(colors->children().back()));
  EXPECT_EQ(tabs->group_model()->GetTabGroup(task)->visual_data()->color(),
            color);
  ASSERT_TRUE(base::test::RunUntil([&] { return !TaskCardWidget(); }));

  const int in_task = tabs->group_model()->GetTabGroup(task)->tab_count();
  card = RightClick(RowOf(task));
  Click(CardRow(card, TaskCard::kNewTabId));
  EXPECT_EQ(tabs->group_model()->GetTabGroup(task)->tab_count(), in_task + 1);
  EXPECT_EQ(tabs->GetActiveTab()->GetGroup(), task);
  ASSERT_TRUE(base::test::RunUntil([&] { return !TaskCardWidget(); }));

  card = RightClick(RowOf(task));
  Click(CardRow(card, TaskCard::kScratchpadId));
  EXPECT_FALSE(tabs->group_model()->ContainsTabGroup(task));
  ASSERT_TRUE(base::test::RunUntil([&] { return !TaskCardWidget(); }));
  // Undo brings the task back under the same id, so its old row goes first.
  ASSERT_TRUE(base::test::RunUntil([&] { return !RowOf(task); }));
  controller->Undo();
  ASSERT_EQ(controller->tree().Tasks().size(), 1u);

  const tab_groups::TabGroupId restored = Task();
  const int count = tabs->count();
  const int closing = tabs->group_model()->GetTabGroup(restored)->tab_count();
  card = RightClick(RowOf(restored));
  Click(CardRow(card, TaskCard::kCloseId));
  ASSERT_TRUE(
      base::test::RunUntil([&] { return tabs->count() == count - closing; }));
  EXPECT_TRUE(controller->tree().Tasks().empty());
}

// A double-click on a task's row turns its name into a field, with the task
// left as open as it was. Return saves the typed name, Esc puts the old one
// back, and a click elsewhere saves it too.
IN_PROC_BROWSER_TEST_F(WindowScreenshotBrowserTest,
                       ADoubleClickRenamesTheTaskOnItsRow) {
  StageSidebar();
  const tab_groups::TabGroupId task = Task();
  TabGroupHeaderView* row = RowOf(task);
  TaskNameField* field = TaskNameField::On(row);
  ASSERT_TRUE(field);
  EXPECT_FALSE(field->editing());
  TabStripModel* tabs = browser()->tab_strip_model();
  const bool collapsed =
      tabs->group_model()->GetTabGroup(task)->visual_data()->is_collapsed();

  Release(row, ui::EF_LEFT_MOUSE_BUTTON);
  Release(row, ui::EF_LEFT_MOUSE_BUTTON | ui::EF_IS_DOUBLE_CLICK);
  ASSERT_TRUE(field->editing());
  EXPECT_TRUE(field->HasFocus());
  EXPECT_EQ(field->GetText(), u"Plan Lisbon trip");
  EXPECT_EQ(field->GetSelectedText(), u"Plan Lisbon trip");
  EXPECT_EQ(
      tabs->group_model()->GetTabGroup(task)->visual_data()->is_collapsed(),
      collapsed);
  field->SetText(u"Lisbon in May");
  SaveWindowScreenshot(browser(), "rename-in-place");
  Type(field, ui::VKEY_RETURN);
  EXPECT_FALSE(field->editing());
  EXPECT_EQ(TitleOf(task), u"Lisbon in May");
  EXPECT_TRUE(TabOrganizationController::From(browser())->HasManualName(
      task.ToString()));

  field->Start();
  field->SetText(u"Porto");
  Type(field, ui::VKEY_ESCAPE);
  EXPECT_FALSE(field->editing());
  EXPECT_EQ(TitleOf(task), u"Lisbon in May");

  field->Start();
  field->SetText(u"Porto");
  field->GetFocusManager()->ClearFocus();
  EXPECT_FALSE(field->editing());
  EXPECT_EQ(TitleOf(task), u"Porto");

  field->Start();
  field->SetText(u"  ");
  Type(field, ui::VKEY_RETURN);
  EXPECT_EQ(TitleOf(task), u"Porto");
}

// Rename Task on the card, Rename Task in the menu bar, and a new task all
// start with the name field. A collapsed sidebar widens while the name is
// typed.
IN_PROC_BROWSER_TEST_F(WindowScreenshotBrowserTest,
                       RenameTaskAndANewTaskStartWithTheNameField) {
  StageSidebar();
  const tab_groups::TabGroupId task = Task();
  TaskNameField* field = TaskNameField::On(RowOf(task));
  ASSERT_TRUE(field);

  views::Widget* card = RightClick(RowOf(task));
  Click(CardRow(card, TaskCard::kRenameId));
  ASSERT_TRUE(base::test::RunUntil([&] { return field->editing(); }));
  ASSERT_TRUE(base::test::RunUntil([&] { return !TaskCardWidget(); }));
  Type(field, ui::VKEY_ESCAPE);

  browser()->tab_strip_model()->ActivateTabAt(browser()
                                                  ->tab_strip_model()
                                                  ->group_model()
                                                  ->GetTabGroup(task)
                                                  ->ListTabs()
                                                  .start());
  ASSERT_TRUE(CanRunWindowCommand(browser(), kRenameTask));
  RunWindowCommand(browser(), kRenameTask);
  ASSERT_TRUE(base::test::RunUntil([&] { return field->editing(); }));
  Type(field, ui::VKEY_ESCAPE);

  TabOrganizationController::OpenNewTask(browser());
  const std::optional<tab_groups::TabGroupId> opened =
      browser()->tab_strip_model()->GetActiveTab()->GetGroup();
  ASSERT_TRUE(opened);
  ASSERT_NE(*opened, task);
  ASSERT_TRUE(base::test::RunUntil([&] {
    TabGroupHeaderView* row = RowOf(*opened);
    return row && TaskNameField::On(row)->editing();
  }));
  TaskNameField* new_field = TaskNameField::On(RowOf(*opened));
  EXPECT_EQ(new_field->GetSelectedText(), u"New task");
  Type(new_field, ui::VKEY_ESCAPE);

  tabs::VerticalTabStripStateController* state =
      tabs::VerticalTabStripStateController::From(browser());
  state->RequestCollapse(true);
  ASSERT_TRUE(base::test::RunUntil([&] { return state->IsCollapsed(); }));
  TabOrganizationController::From(browser())->RenameOnItsRow(task.ToString());
  ASSERT_TRUE(base::test::RunUntil([&] { return field->editing(); }));
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return field->IsDrawn() && field->width() > 0; }));
  SaveWindowScreenshot(browser(), "rename-collapsed");
  Type(field, ui::VKEY_ESCAPE);
}

IN_PROC_BROWSER_TEST_F(WindowScreenshotBrowserTest, TheTaskCardInDarkMode) {
  ThemeServiceFactory::GetForProfile(browser()->GetProfile())
      ->SetBrowserColorScheme(ThemeService::BrowserColorScheme::kDark);
  StageSidebar();
  SetUpAModel();
  views::Widget* card = RightClick(RowOf(Task()));
  ASSERT_TRUE(card);
  base::RunLoop().RunUntilIdle();
  SaveWidgetScreenshot(card, "task-card-dark");
}

}  // namespace
}  // namespace uw
