// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#import <AppKit/AppKit.h>

#include <optional>

#include "chrome/browser/headless/test/headless_mode_browsertest.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/tabs/new_tab_button.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "testing/gtest_mac.h"
#include "ui/base/accelerators/accelerator.h"
#include "ui/events/event_constants.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "uw/browser/terminal/terminal_tab.h"

namespace uw {
namespace {

using UwTerminalMenuBrowserTest = headless::HeadlessModeBrowserTest;

// "New terminal" is in the File menu with Ctrl+` and in the menu of the
// sidebar's "+" button. macOS runs a menu's shortcut only for the window in
// front, which a headless test does not have, so the shortcut itself is not
// pressed here.
IN_PROC_BROWSER_TEST_F(UwTerminalMenuBrowserTest,
                       NewTerminalIsInTheFileAndPlusMenus) {
  NSMenuItem* item = nil;
  for (NSMenuItem* menu in NSApp.mainMenu.itemArray) {
    item = item ?: [menu.submenu itemWithTag:kNewTerminalCommand];
  }
  ASSERT_TRUE(item);
  EXPECT_NSEQ(item.keyEquivalent, @"`");
  EXPECT_EQ(item.keyEquivalentModifierMask, NSEventModifierFlagControl);

  NewTabButtonMenuModel plus(browser());
  const std::optional<size_t> index =
      plus.GetIndexOfCommandId(kNewTerminalCommand);
  ASSERT_TRUE(index);
  ui::Accelerator shortcut;
  EXPECT_TRUE(plus.GetAcceleratorAt(*index, &shortcut));
  EXPECT_EQ(shortcut, ui::Accelerator(ui::VKEY_OEM_3, ui::EF_CONTROL_DOWN));

  const int before = browser()->tab_strip_model()->count();
  plus.ActivatedAt(*index);
  EXPECT_EQ(browser()->tab_strip_model()->count(), before + 1);
  EXPECT_TRUE(IsTerminalUrl(
      browser()->tab_strip_model()->GetActiveWebContents()->GetVisibleURL()));
}

}  // namespace
}  // namespace uw
