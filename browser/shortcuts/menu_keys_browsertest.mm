// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/browser/shortcuts/menu_keys.h"

#import <AppKit/AppKit.h>

#include <string>
#include <vector>

#include "base/run_loop.h"
#include "base/test/run_until.h"
#include "chrome/app/chrome_command_ids.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/global_keyboard_shortcuts_mac.h"
#include "chrome/browser/headless/test/headless_mode_browsertest.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "ui/base/accelerators/accelerator.h"
#include "ui/events/keycodes/keyboard_codes.h"
#import "ui/events/test/cocoa_test_event_utils.h"
#include "url/gurl.h"
#include "uw/browser/tab_organization/commands.h"
#include "uw/browser/tab_organization/tab_organization_controller.h"

namespace uw {
namespace {

constexpr NSEventModifierFlags kCmd = NSEventModifierFlagCommand;
constexpr NSEventModifierFlags kCtrlCmd =
    NSEventModifierFlagControl | NSEventModifierFlagCommand;

class MenuKeysBrowserTest : public headless::HeadlessModeBrowserTest {
 protected:
  PrefService& local_state() { return *g_browser_process->local_state(); }

  NSWindow* window() {
    return browser()->GetWindow()->GetNativeWindow().GetNativeNSWindow();
  }

  NSEvent* Key(ui::KeyboardCode key, NSEventModifierFlags modifiers) {
    return cocoa_test_event_utils::SynthesizeKeyEvent(window(),
                                                      /*keyDown=*/true, key,
                                                      modifiers);
  }

  // The command a key runs, as the window looks it up.
  int CommandFor(ui::KeyboardCode key, NSEventModifierFlags modifiers) {
    return CommandForKeyEvent(Key(key, modifiers)).chrome_command;
  }

  // Hands a key to the window the way macOS hands it a shortcut.
  void Press(ui::KeyboardCode key, NSEventModifierFlags modifiers) {
    [window() performKeyEquivalent:Key(key, modifiers)];
  }

  // The command of the menu bar called `title`.
  MenuKey Row(const std::string& title) {
    for (const MenuKey& key : MenuKeys()) {
      if (key.title == title) {
        return key;
      }
    }
    ADD_FAILURE() << "No command called " << title;
    return MenuKey();
  }

  bool SelectedTabIsKept() {
    TabOrganizationController* controller =
        TabOrganizationController::From(browser());
    return controller->IsKept(
        controller->IdFor(browser()->tab_strip_model()->GetActiveTab())
            .value_or(PageId()));
  }
};

IN_PROC_BROWSER_TEST_F(MenuKeysBrowserTest, TheSectionListsEveryMenuOfTheBar) {
  const MenuKey new_tab = Row("New Tab");
  EXPECT_EQ("Command+T", new_tab.key);
  EXPECT_TRUE(new_tab.name.empty());

  const MenuKey keep = Row("Keep Here");
  EXPECT_EQ("Tasks", keep.menu);
  EXPECT_EQ("keep_here", keep.name);
  EXPECT_EQ("Command+Ctrl+K", keep.key);
  EXPECT_EQ(keep.key, keep.default_key);

  EXPECT_EQ("Command+Ctrl+Shift+N", Row("Move to New Task").key);
  EXPECT_EQ("go_to_task_3", Row("Task 3").name);
  EXPECT_EQ("Tasks › Go to Task", Row("Task 3").menu);
  EXPECT_EQ("keyboard_shortcuts", Row("Keyboard Shortcuts…").name);
  EXPECT_EQ("uw", Row("Quit uw").menu);
}

// Cmd+S hides and shows the sidebar, as in Arc. Save Page As keeps no key, and
// Chromium's Cmd+Shift+L for the sidebar is gone.
IN_PROC_BROWSER_TEST_F(MenuKeysBrowserTest, CmdSHidesTheSidebar) {
  EXPECT_EQ(IDC_TOGGLE_VERTICAL_TABS_COLLAPSE, CommandFor(ui::VKEY_S, kCmd));
  EXPECT_EQ("Command+S", Row("Hide Sidebar").key);
  EXPECT_NE(IDC_TOGGLE_VERTICAL_TABS_COLLAPSE,
            CommandFor(ui::VKEY_L, kCmd | NSEventModifierFlagShift));
  ui::Accelerator key;
  EXPECT_FALSE(GetDefaultMacAcceleratorForCommandId(IDC_SAVE_PAGE, &key));
}

IN_PROC_BROWSER_TEST_F(MenuKeysBrowserTest, ANewKeyRunsItsCommandInTheWindow) {
  ASSERT_TRUE(SetMenuKey(local_state(), "keep_here", "Command+Ctrl+J"));

  EXPECT_EQ(TabOrganizationController::kKeepHere,
            CommandFor(ui::VKEY_J, kCtrlCmd));
  EXPECT_NE(TabOrganizationController::kKeepHere,
            CommandFor(ui::VKEY_K, kCtrlCmd));

  ASSERT_FALSE(SelectedTabIsKept());
  Press(ui::VKEY_J, kCtrlCmd);
  EXPECT_TRUE(base::test::RunUntil([&] { return SelectedTabIsKept(); }));

  ui::Accelerator key;
  ASSERT_TRUE(GetDefaultMacAcceleratorForCommandId(
      TabOrganizationController::kKeepHere, &key));
  EXPECT_EQ(
      ui::Accelerator(ui::VKEY_J, ui::EF_COMMAND_DOWN | ui::EF_CONTROL_DOWN),
      key);
}

IN_PROC_BROWSER_TEST_F(MenuKeysBrowserTest, AKeyWithoutCommandIsRefused) {
  EXPECT_FALSE(SetMenuKey(local_state(), "keep_here", "Ctrl+K"));
  EXPECT_FALSE(SetMenuKey(local_state(), "keep_here", "Command+Return"));
  EXPECT_FALSE(SetMenuKey(local_state(), "no_such_command", "Command+J"));
  EXPECT_TRUE(local_state().GetDict(kMenuKeysPref).empty());
  EXPECT_EQ("Command+Ctrl+K", Row("Keep Here").key);
}

IN_PROC_BROWSER_TEST_F(MenuKeysBrowserTest, TakingAKeyFromChromiumAndBack) {
  ASSERT_EQ(IDC_FIND_NEXT, CommandFor(ui::VKEY_G, kCmd));
  ASSERT_TRUE(SetMenuKey(local_state(), "move_to_new_task", "Command+G"));

  EXPECT_EQ(TabOrganizationController::kNewTask, CommandFor(ui::VKEY_G, kCmd));
  const MenuKey find_next = Row("Find Next");
  EXPECT_EQ("", find_next.key);
  EXPECT_EQ("Command+G", find_next.default_key);
  EXPECT_EQ("Move to New Task", find_next.taken_by);
  ui::Accelerator key;
  EXPECT_FALSE(GetDefaultMacAcceleratorForCommandId(IDC_FIND_NEXT, &key));

  // Reset on Find Next's row gives the key back, and Move to New Task is
  // left with none.
  GiveBackMenuKey(local_state(), "Command+G");
  EXPECT_EQ(IDC_FIND_NEXT, CommandFor(ui::VKEY_G, kCmd));
  EXPECT_EQ("", Row("Move to New Task").key);

  ResetMenuKey(local_state(), "move_to_new_task");
  EXPECT_EQ("Command+Ctrl+Shift+N", Row("Move to New Task").key);
  EXPECT_TRUE(local_state().GetDict(kMenuKeysPref).empty());
}

IN_PROC_BROWSER_TEST_F(MenuKeysBrowserTest, TakingAKeyFromAnotherUwCommand) {
  ASSERT_TRUE(SetMenuKey(local_state(), "keep_here", "Command+Ctrl+S"));

  EXPECT_EQ(TabOrganizationController::kKeepHere,
            CommandFor(ui::VKEY_S, kCtrlCmd));
  const MenuKey scratchpad = Row("Move to Scratchpad");
  EXPECT_EQ("", scratchpad.key);
  EXPECT_EQ("Keep Here", scratchpad.taken_by);

  // Its Reset takes the key back, and Keep Here is left with none.
  ResetMenuKey(local_state(), "move_to_scratchpad");
  EXPECT_EQ(TabOrganizationController::kScratchpad,
            CommandFor(ui::VKEY_S, kCtrlCmd));
  EXPECT_EQ("", Row("Keep Here").key);

  ResetMenuKeys(local_state());
  EXPECT_EQ("Command+Ctrl+K", Row("Keep Here").key);
  EXPECT_EQ("Command+Ctrl+S", Row("Move to Scratchpad").key);
}

// New Task's key is also one of Chromium's keys outside the menu bar, which
// stops with it.
IN_PROC_BROWSER_TEST_F(MenuKeysBrowserTest, AChangedKeyStopsItsOldKey) {
  ASSERT_EQ(IDC_CREATE_NEW_TAB_GROUP, CommandFor(ui::VKEY_N, kCtrlCmd));
  ASSERT_TRUE(SetMenuKey(local_state(), "new_task", "Command+Ctrl+J"));

  EXPECT_EQ(IDC_CREATE_NEW_TAB_GROUP, CommandFor(ui::VKEY_J, kCtrlCmd));
  EXPECT_NE(IDC_CREATE_NEW_TAB_GROUP, CommandFor(ui::VKEY_N, kCtrlCmd));

  ASSERT_TRUE(SetMenuKey(local_state(), "new_task", ""));
  EXPECT_NE(IDC_CREATE_NEW_TAB_GROUP, CommandFor(ui::VKEY_J, kCtrlCmd));
  EXPECT_EQ("", Row("New Task").key);
  EXPECT_EQ("", Row("New Task").taken_by);
}

IN_PROC_BROWSER_TEST_F(MenuKeysBrowserTest, PRE_AKeyStaysAfterARestart) {
  ASSERT_TRUE(SetMenuKey(local_state(), "keep_here", "Command+G"));
}

IN_PROC_BROWSER_TEST_F(MenuKeysBrowserTest, AKeyStaysAfterARestart) {
  EXPECT_EQ(TabOrganizationController::kKeepHere, CommandFor(ui::VKEY_G, kCmd));
  EXPECT_EQ("Keep Here", Row("Find Next").taken_by);
}

IN_PROC_BROWSER_TEST_F(MenuKeysBrowserTest, HelpOpensTheSection) {
  chrome::ExecuteCommand(browser(), kShowShortcutSettings);
  EXPECT_TRUE(base::test::RunUntil([&] {
    return browser()->tab_strip_model()->GetActiveWebContents()->GetURL() ==
           GURL("chrome://settings/shortcuts");
  }));
}

}  // namespace
}  // namespace uw
