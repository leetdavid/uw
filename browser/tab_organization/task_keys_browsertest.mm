// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#import <AppKit/AppKit.h>

#include <optional>
#include <string>
#include <vector>

#include "base/run_loop.h"
#include "base/test/run_until.h"
#include "chrome/app/chrome_command_ids.h"
#include "chrome/browser/global_keyboard_shortcuts_mac.h"
#include "chrome/browser/headless/test/headless_mode_browsertest.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_element_identifiers.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/browser/ui/navigator/browser_navigator.h"
#include "chrome/browser/ui/navigator/browser_navigator_params.h"
#include "chrome/browser/ui/tabs/tab_group_model.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "components/prefs/pref_service.h"
#include "components/tab_groups/tab_group_visual_data.h"
#include "components/tabs/public/tab_group.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/test/browser_test.h"
#include "testing/gtest_mac.h"
#include "ui/base/accelerators/accelerator.h"
#include "ui/events/event_constants.h"
#include "ui/events/keycodes/keyboard_codes.h"
#import "ui/events/test/cocoa_test_event_utils.h"
#include "ui/views/view_utils.h"
#include "url/gurl.h"
#include "uw/browser/tab_organization/ai_connection.h"
#include "uw/browser/tab_organization/commands.h"
#include "uw/browser/tab_organization/tab_organization_controller.h"
#include "uw/browser/ui/views/tab_organization/task_name_field.h"
#include "uw/browser/ui/views/tab_organization/window_commands.h"

namespace uw {
namespace {

constexpr NSEventModifierFlags kCtrlCmd =
    NSEventModifierFlagControl | NSEventModifierFlagCommand;
constexpr NSEventModifierFlags kOptionCmd =
    NSEventModifierFlagOption | NSEventModifierFlagCommand;

// The name field on the row of `task` under `view`.
TaskNameField* NameFieldOf(views::View* view,
                           const tab_groups::TabGroupId& task) {
  if (TaskNameField* field = views::AsViewClass<TaskNameField>(view);
      field && field->group() == task) {
    return field;
  }
  for (views::View* child : view->children()) {
    if (TaskNameField* field = NameFieldOf(child, task)) {
      return field;
    }
  }
  return nullptr;
}

NSMenuItem* MenuItemWithTag(NSMenu* menu, int tag) {
  for (NSMenuItem* item in menu.itemArray) {
    if (item.tag == tag && !item.submenu) {
      return item;
    }
    if (NSMenuItem* found =
            item.submenu ? MenuItemWithTag(item.submenu, tag) : nil) {
      return found;
    }
  }
  return nil;
}

class TaskKeysBrowserTest : public headless::HeadlessModeBrowserTest {
 protected:
  TabOrganizationController* controller() {
    return TabOrganizationController::From(browser());
  }

  TabStripModel* tabs() { return browser()->tab_strip_model(); }

  // Hands a key to the window the way macOS hands it a shortcut. The page in
  // front sees it first, as it would in use.
  void Press(ui::KeyboardCode key, NSEventModifierFlags modifiers) {
    NSWindow* window =
        browser()->GetWindow()->GetNativeWindow().GetNativeNSWindow();
    [window performKeyEquivalent:cocoa_test_event_utils::SynthesizeKeyEvent(
                                     window, /*keyDown=*/true, key, modifiers)];
  }

  // Types a key into the focused view, the way macOS hands the window a key
  // that is no shortcut.
  void Type(ui::KeyboardCode key) {
    NSWindow* window =
        browser()->GetWindow()->GetNativeWindow().GetNativeNSWindow();
    [window.contentView keyDown:cocoa_test_event_utils::SynthesizeKeyEvent(
                                    window, /*keyDown=*/true, key, 0)];
  }

  tabs::TabInterface* NewTab() {
    chrome::NewTab(browser(), NewTabTypes::kNewTabCommand);
    base::RunLoop().RunUntilIdle();
    return tabs()->GetActiveTab();
  }

  tabs::TabInterface* OpenLinkFrom(tabs::TabInterface* source) {
    NavigateParams params(browser(), GURL("about:blank"),
                          ui::PAGE_TRANSITION_LINK);
    params.disposition = WindowOpenDisposition::NEW_BACKGROUND_TAB;
    params.source_contents = source->GetContents();
    Navigate(&params);
    base::RunLoop().RunUntilIdle();
    return tabs()->GetTabForWebContents(params.navigated_or_inserted_contents);
  }

  void Select(tabs::TabInterface* tab) {
    tabs()->ActivateTabAt(tabs()->GetIndexOfTab(tab));
  }

  PageId Id(tabs::TabInterface* tab) {
    return controller()->IdFor(tab).value_or(PageId());
  }

  // Ids in tab order with two spaces per nesting level.
  std::vector<std::string> Rows() {
    std::vector<std::string> rows;
    for (const Page& page : controller()->tree().pages()) {
      rows.push_back(
          std::string(controller()->tree().DepthOf(page.id) * 2, ' ') +
          page.id.value());
    }
    return rows;
  }
};

IN_PROC_BROWSER_TEST_F(TaskKeysBrowserTest,
                       EveryCommandIsInTheMenuBarWithItsKey) {
  struct Expected {
    int command;
    NSString* key;
    NSEventModifierFlags modifiers;
  };
  NSString* right = [NSString
      stringWithFormat:@"%C", static_cast<unichar>(NSRightArrowFunctionKey)];
  NSString* left = [NSString
      stringWithFormat:@"%C", static_cast<unichar>(NSLeftArrowFunctionKey)];
  NSString* up = [NSString
      stringWithFormat:@"%C", static_cast<unichar>(NSUpArrowFunctionKey)];
  NSString* down = [NSString
      stringWithFormat:@"%C", static_cast<unichar>(NSDownArrowFunctionKey)];
  const std::vector<Expected> expected = {
      {TabOrganizationController::kNest, right, kCtrlCmd},
      {TabOrganizationController::kOutdent, left, kCtrlCmd},
      {TabOrganizationController::kMoveUp, up, kCtrlCmd},
      {TabOrganizationController::kMoveDown, down, kCtrlCmd},
      {TabOrganizationController::kCloseBranch, @"w", kOptionCmd},
      {TabOrganizationController::kToggleBranch, @".", kCtrlCmd},
      {TabOrganizationController::kNewChildTab, @"t", kCtrlCmd},
      {IDC_CREATE_NEW_TAB_GROUP, @"n", kCtrlCmd},
      {TabOrganizationController::kNewTask, @"N", kCtrlCmd},
      {TabOrganizationController::kScratchpad, @"s", kCtrlCmd},
      {kGoToTask1, @"1", kCtrlCmd},
      {kGoToTask9, @"9", kCtrlCmd},
      {kGoToScratchpad, @"0", kCtrlCmd},
      {kRenameTask, @"r", kCtrlCmd},
      {IDC_CLOSE_TAB_GROUP, @"w", kCtrlCmd},
      {kConfirmSuggestion, @"\r", kCtrlCmd},
      {TabOrganizationController::kUndo, @"z", kOptionCmd},
      {kNameTask, @"", 0},
      {kShowOrganizerActivity, @"", 0},
      {kPauseOrganizationForAnHour, @"", 0},
      {kPauseOrganizationUntilLaunch, @"", 0},
      {kPauseOrganizationIndefinitely, @"", 0},
      {kResumeOrganization, @"", 0},
      {kShowOrganizationSettings, @"", 0},
  };
  for (const Expected& item : expected) {
    SCOPED_TRACE(item.command);
    NSMenuItem* menu_item = MenuItemWithTag(NSApp.mainMenu, item.command);
    ASSERT_TRUE(menu_item);
    EXPECT_NSEQ(menu_item.keyEquivalent, item.key);
    if (item.key.length) {
      EXPECT_EQ(menu_item.keyEquivalentModifierMask, item.modifiers);
    }
  }
  EXPECT_NSEQ(
      MenuItemWithTag(NSApp.mainMenu, TabOrganizationController::kScratchpad)
          .title,
      @"Move to Scratchpad");

  ui::Accelerator nest;
  ASSERT_TRUE(GetCommandAccelerator(browser(), TabOrganizationController::kNest,
                                    &nest));
  EXPECT_EQ(nest, ui::Accelerator(ui::VKEY_RIGHT,
                                  ui::EF_CONTROL_DOWN | ui::EF_COMMAND_DOWN));
}

IN_PROC_BROWSER_TEST_F(TaskKeysBrowserTest, ArrowKeysNestAndMoveBranches) {
  tabs::TabInterface* first = tabs()->GetTabAtIndex(0);
  tabs::TabInterface* second = NewTab();
  tabs::TabInterface* third = NewTab();
  const std::string a = Id(first).value();
  const std::string b = Id(second).value();
  const std::string c = Id(third).value();

  Select(second);
  Press(ui::VKEY_RIGHT, kCtrlCmd);
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return Rows() == std::vector<std::string>{a, "  " + b, c}; }));

  Press(ui::VKEY_LEFT, kCtrlCmd);
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return Rows() == std::vector<std::string>{a, b, c}; }));

  Press(ui::VKEY_UP, kCtrlCmd);
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return Rows() == std::vector<std::string>{b, a, c}; }));

  Press(ui::VKEY_DOWN, kCtrlCmd);
  Press(ui::VKEY_DOWN, kCtrlCmd);
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return Rows() == std::vector<std::string>{a, c, b}; }));
  EXPECT_EQ(tabs()->GetIndexOfTab(second), 2);
}

IN_PROC_BROWSER_TEST_F(TaskKeysBrowserTest,
                       CtrlCmdTOpensAChildFirstUnderTheSelectedTab) {
  tabs::TabInterface* parent = tabs()->GetTabAtIndex(0);
  tabs::TabInterface* link = OpenLinkFrom(parent);
  tabs::TabInterface* other = NewTab();
  const std::string p = Id(parent).value();
  const std::string l = Id(link).value();
  const std::string o = Id(other).value();
  Select(parent);

  Press(ui::VKEY_T, kCtrlCmd);
  ASSERT_TRUE(base::test::RunUntil([&] { return tabs()->count() == 4; }));
  tabs::TabInterface* child = tabs()->GetActiveTab();
  const std::string c = Id(child).value();
  ASSERT_TRUE(base::test::RunUntil([&] {
    return Rows() == std::vector<std::string>{p, "  " + c, "  " + l, o};
  }));
  EXPECT_TRUE(
      base::test::RunUntil([&] { return tabs()->GetIndexOfTab(child) == 1; }));
}

IN_PROC_BROWSER_TEST_F(TaskKeysBrowserTest,
                       CtrlCmdPeriodCollapsesAndExpandsTheBranch) {
  tabs::TabInterface* parent = tabs()->GetTabAtIndex(0);
  tabs::TabInterface* child = OpenLinkFrom(parent);
  Select(parent);
  EXPECT_EQ(GetWindowCommandTitle(browser(),
                                  TabOrganizationController::kToggleBranch),
            u"Collapse Branch");

  Press(ui::VKEY_OEM_PERIOD, kCtrlCmd);
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return controller()->IsCollapsed(Id(parent)); }));
  EXPECT_TRUE(controller()->IsHidden(Id(child)));
  EXPECT_EQ(GetWindowCommandTitle(browser(),
                                  TabOrganizationController::kToggleBranch),
            u"Expand Branch");

  Press(ui::VKEY_OEM_PERIOD, kCtrlCmd);
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return !controller()->IsCollapsed(Id(parent)); }));
  EXPECT_FALSE(controller()->IsHidden(Id(child)));
}

IN_PROC_BROWSER_TEST_F(TaskKeysBrowserTest,
                       KeysMoveToANewTaskToTheScratchpadAndCloseABranch) {
  tabs::TabInterface* parent = tabs()->GetTabAtIndex(0);
  tabs::TabInterface* child = OpenLinkFrom(parent);
  NewTab();
  Select(parent);

  Press(ui::VKEY_N, kCtrlCmd | NSEventModifierFlagShift);
  ASSERT_TRUE(
      base::test::RunUntil([&] { return parent->GetGroup().has_value(); }));
  EXPECT_EQ(child->GetGroup(), parent->GetGroup());

  Press(ui::VKEY_S, kCtrlCmd);
  ASSERT_TRUE(base::test::RunUntil([&] { return !parent->GetGroup(); }));
  EXPECT_FALSE(child->GetGroup());

  const int before = tabs()->count();
  Press(ui::VKEY_W, kOptionCmd);
  ASSERT_TRUE(
      base::test::RunUntil([&] { return tabs()->count() == before - 2; }));
}

// Ctrl+Cmd+N is New task in the menu bar, which a headless window never
// reaches, so the test asks which command each key runs. Chromium's
// Ctrl+Cmd+P for a new tab group runs nothing.
IN_PROC_BROWSER_TEST_F(TaskKeysBrowserTest, CtrlCmdNIsNewTask) {
  NSWindow* window =
      browser()->GetWindow()->GetNativeWindow().GetNativeNSWindow();
  const auto command = [&](ui::KeyboardCode key,
                           NSEventModifierFlags modifiers) {
    return CommandForKeyEvent(cocoa_test_event_utils::SynthesizeKeyEvent(
                                  window, /*keyDown=*/true, key, modifiers))
        .chrome_command;
  };
  EXPECT_EQ(command(ui::VKEY_N, kCtrlCmd), IDC_CREATE_NEW_TAB_GROUP);
  EXPECT_EQ(command(ui::VKEY_N, kCtrlCmd | NSEventModifierFlagShift),
            TabOrganizationController::kNewTask);
  EXPECT_EQ(command(ui::VKEY_P, kCtrlCmd), NO_COMMAND);
}

IN_PROC_BROWSER_TEST_F(TaskKeysBrowserTest,
                       NumberKeysGoToTheTabLastUsedInATask) {
  tabs::TabInterface* loose = tabs()->GetTabAtIndex(0);
  tabs::TabInterface* plan = NewTab();
  tabs::TabInterface* plan_notes = NewTab();
  tabs::TabInterface* trip = NewTab();
  ASSERT_TRUE(controller()->MoveToNewTask({Id(plan), Id(plan_notes)}, u"Plan"));
  ASSERT_TRUE(controller()->MoveToNewTask({Id(trip)}, u"Trip"));
  base::RunLoop().RunUntilIdle();
  Select(plan_notes);
  Select(trip);

  Press(ui::VKEY_1, kCtrlCmd);
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return tabs()->GetActiveTab() == plan_notes; }));

  Press(ui::VKEY_0, kCtrlCmd);
  ASSERT_TRUE(
      base::test::RunUntil([&] { return tabs()->GetActiveTab() == loose; }));

  Press(ui::VKEY_2, kCtrlCmd);
  ASSERT_TRUE(
      base::test::RunUntil([&] { return tabs()->GetActiveTab() == trip; }));

  Press(ui::VKEY_3, kCtrlCmd);
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(tabs()->GetActiveTab(), trip);
  EXPECT_FALSE(CanRunWindowCommand(browser(), kGoToTask1 + 2));
}

// In the sidebar the task's name turns into a field on its row.
IN_PROC_BROWSER_TEST_F(TaskKeysBrowserTest, CtrlCmdRRenamesTheTaskOnItsRow) {
  tabs::TabInterface* tab = tabs()->GetTabAtIndex(0);
  const std::optional<tab_groups::TabGroupId> task =
      controller()->MoveToNewTask({Id(tab)}, u"Plan");
  ASSERT_TRUE(task);
  base::RunLoop().RunUntilIdle();
  TaskNameField* field =
      NameFieldOf(BrowserView::GetBrowserViewForBrowser(browser()), *task);
  ASSERT_TRUE(field);
  EXPECT_FALSE(field->editing());

  Press(ui::VKEY_R, kCtrlCmd);
  ASSERT_TRUE(base::test::RunUntil([&] { return field->editing(); }));
  EXPECT_EQ(field->GetText(), u"Plan");
}

// Space goes into the name, though on the row it opens or collapses the task.
IN_PROC_BROWSER_TEST_F(TaskKeysBrowserTest, SpaceTypesIntoTheNameField) {
  tabs::TabInterface* tab = tabs()->GetTabAtIndex(0);
  const std::optional<tab_groups::TabGroupId> task =
      controller()->MoveToNewTask({Id(tab)}, u"Plan");
  ASSERT_TRUE(task);
  base::RunLoop().RunUntilIdle();
  TaskNameField* field =
      NameFieldOf(BrowserView::GetBrowserViewForBrowser(browser()), *task);
  ASSERT_TRUE(field);
  field->Start();
  ASSERT_TRUE(field->HasFocus());
  field->SetText(u"Plan");

  Type(ui::VKEY_SPACE);
  Type(ui::VKEY_A);
  EXPECT_EQ(field->GetText(), u"Plan a");
  EXPECT_FALSE(
      tabs()->group_model()->GetTabGroup(*task)->visual_data()->is_collapsed());
}

IN_PROC_BROWSER_TEST_F(TaskKeysBrowserTest,
                       MenuOnlyCommandsPauseAndConfirmOnlyWhenTheyCan) {
  EXPECT_FALSE(CanRunWindowCommand(browser(), kConfirmSuggestion));
  EXPECT_FALSE(CanRunWindowCommand(browser(), kResumeOrganization));
  EXPECT_FALSE(CanRunWindowCommand(browser(), kRenameTask));
  EXPECT_FALSE(CanRunWindowCommand(browser(), kPauseOrganizationForAnHour));

  PrefService* prefs = browser()->GetProfile()->GetPrefs();
  prefs->SetBoolean(kOrganizationEnabledPref, true);
  EXPECT_TRUE(CanRunWindowCommand(browser(), kPauseOrganizationForAnHour));
  RunWindowCommand(browser(), kPauseOrganizationIndefinitely);
  EXPECT_TRUE(CanRunWindowCommand(browser(), kResumeOrganization));
  EXPECT_FALSE(CanRunWindowCommand(browser(), kPauseOrganizationIndefinitely));

  // Turned off while paused, every pause item is gray, Resume too.
  prefs->SetBoolean(kOrganizationEnabledPref, false);
  EXPECT_FALSE(CanRunWindowCommand(browser(), kResumeOrganization));
  EXPECT_FALSE(CanRunWindowCommand(browser(), kPauseOrganizationForAnHour));

  const bool shown = IsWindowCommandChecked(browser(), kShowOrganizerActivity);
  RunWindowCommand(browser(), kShowOrganizerActivity);
  EXPECT_NE(IsWindowCommandChecked(browser(), kShowOrganizerActivity), shown);
}

}  // namespace
}  // namespace uw
