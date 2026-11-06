// Copyright 2026 The uw Authors. All rights reserved.

#include <vector>

#include "base/test/run_until.h"
#include "chrome/app/chrome_command_ids.h"
#include "chrome/browser/headless/test/headless_mode_browsertest.h"
#include "chrome/browser/ui/accelerator_table.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/common/webui_url_constants.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "ui/base/accelerators/accelerator.h"
#include "ui/events/event_constants.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/views/focus/focus_manager.h"
#include "uw/browser/tab_organization/commands.h"
#include "uw/browser/tab_organization/tab_organization_controller.h"

namespace uw {
namespace {

using WindowsKeysBrowserTest = headless::HeadlessModeBrowserTest;

// Ctrl+, opens settings, as Cmd+, does on the Mac, and the ⋮ menu shows it.
IN_PROC_BROWSER_TEST_F(WindowsKeysBrowserTest, CtrlCommaOpensSettings) {
  const ui::Accelerator ctrl_comma(ui::VKEY_OEM_COMMA, ui::EF_CONTROL_DOWN);
  BrowserView* view = BrowserView::GetBrowserViewForBrowser(browser());
  ui::Accelerator shown;
  ASSERT_TRUE(view->GetAccelerator(IDC_OPTIONS, &shown));
  EXPECT_EQ(shown, ctrl_comma);

  ASSERT_TRUE(view->GetFocusManager()->ProcessAccelerator(ctrl_comma));
  EXPECT_TRUE(base::test::RunUntil([&] {
    content::WebContents* page =
        browser()->tab_strip_model()->GetActiveWebContents();
    return page &&
           page->GetVisibleURL().host() == chrome::kChromeUISettingsHost;
  }));
}

// Ctrl+Alt keys act on the selected tab and the tabs under it, and Alt+Shift
// keys on its task, as Option+Cmd and Ctrl+Cmd keys do on the Mac. See
// docs/adr/0032.
IN_PROC_BROWSER_TEST_F(WindowsKeysBrowserTest, KeysSayTheirScope) {
  using Controller = TabOrganizationController;
  constexpr int kCtrlAlt = ui::EF_CONTROL_DOWN | ui::EF_ALT_DOWN;
  constexpr int kAltShift = ui::EF_ALT_DOWN | ui::EF_SHIFT_DOWN;
  const struct {
    ui::KeyboardCode key;
    int modifiers;
    int command;
  } expected[] = {
      {ui::VKEY_T, kCtrlAlt, Controller::kNewChildTab},
      {ui::VKEY_S, kCtrlAlt, Controller::kScratchpad},
      {ui::VKEY_K, kCtrlAlt, Controller::kKeepHere},
      {ui::VKEY_OEM_PERIOD, kCtrlAlt, Controller::kToggleBranch},
      {ui::VKEY_OEM_4, kCtrlAlt, Controller::kMoveToPreviousTask},
      {ui::VKEY_OEM_6, kCtrlAlt, Controller::kMoveToNextTask},
      {ui::VKEY_W, kCtrlAlt, Controller::kCloseBranch},
      {ui::VKEY_Z, kCtrlAlt, Controller::kUndo},
      {ui::VKEY_N, kAltShift, IDC_CREATE_NEW_TAB_GROUP},
      {ui::VKEY_W, kAltShift, IDC_CLOSE_TAB_GROUP},
      {ui::VKEY_X, kAltShift, IDC_FOCUS_NEXT_TAB_GROUP},
      {ui::VKEY_Z, kAltShift, IDC_FOCUS_PREV_TAB_GROUP},
      {ui::VKEY_R, kAltShift, kRenameTask},
      {ui::VKEY_1, kAltShift, kGoToTask1},
      {ui::VKEY_9, kAltShift, kGoToTask9},
      {ui::VKEY_0, kAltShift, kGoToScratchpad},
      {ui::VKEY_RETURN, kAltShift, kConfirmSuggestion},
      {ui::VKEY_RIGHT, kAltShift, Controller::kNest},
      {ui::VKEY_LEFT, kAltShift, Controller::kOutdent},
      {ui::VKEY_UP, kAltShift, Controller::kMoveUp},
      {ui::VKEY_DOWN, kAltShift, Controller::kMoveDown},
      // Chromium's Alt+Shift+P for a new tab group gave way to Alt+Shift+N.
      {ui::VKEY_P, kAltShift, -1},
  };
  const std::vector<AcceleratorMapping> table = GetAcceleratorList();
  for (const auto& item : expected) {
    SCOPED_TRACE(ui::Accelerator(item.key, item.modifiers).GetShortcutText());
    std::vector<int> commands;
    for (const AcceleratorMapping& mapping : table) {
      if (mapping.keycode == item.key && mapping.modifiers == item.modifiers) {
        commands.push_back(mapping.command_id);
      }
    }
    EXPECT_EQ(commands, item.command < 0 ? std::vector<int>()
                                         : std::vector<int>{item.command});
  }
}

// Ctrl+Alt+] moves the selected tab from the last task on to the Scratchpad,
// and Ctrl+Alt+[ back to the bottom of that task.
IN_PROC_BROWSER_TEST_F(WindowsKeysBrowserTest, CtrlAltBracketsStepTheTab) {
  BrowserView* view = BrowserView::GetBrowserViewForBrowser(browser());
  TabStripModel* tabs = browser()->tab_strip_model();
  TabOrganizationController* controller =
      TabOrganizationController::From(browser());
  const PageId stays = controller->IdFor(tabs->GetActiveTab()).value();
  chrome::NewTab(browser(), NewTabTypes::kNewTabCommand);
  ASSERT_TRUE(base::test::RunUntil([&] { return tabs->count() == 2; }));
  ASSERT_TRUE(controller->MoveToNewTask(
      {stays, controller->IdFor(tabs->GetActiveTab()).value()}, u"Trip"));
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return tabs->GetActiveTab()->GetGroup().has_value(); }));

  ASSERT_TRUE(view->GetFocusManager()->ProcessAccelerator(
      ui::Accelerator(ui::VKEY_OEM_6, ui::EF_CONTROL_DOWN | ui::EF_ALT_DOWN)));
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return !tabs->GetActiveTab()->GetGroup().has_value(); }));

  ASSERT_TRUE(view->GetFocusManager()->ProcessAccelerator(
      ui::Accelerator(ui::VKEY_OEM_4, ui::EF_CONTROL_DOWN | ui::EF_ALT_DOWN)));
  EXPECT_TRUE(base::test::RunUntil(
      [&] { return tabs->GetActiveTab()->GetGroup().has_value(); }));
}

}  // namespace
}  // namespace uw
