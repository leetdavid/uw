// Copyright 2026 The uw Authors. All rights reserved.

#import <AppKit/AppKit.h>

#include <optional>

#include "base/test/run_until.h"
#include "chrome/browser/headless/test/headless_mode_browsertest.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/test/browser_test.h"
#include "ui/events/keycodes/keyboard_codes.h"
#import "ui/events/test/cocoa_test_event_utils.h"
#include "uw/browser/tab_organization/tab_organization_controller.h"

namespace uw {
namespace {

class UndoShortcutBrowserTest : public headless::HeadlessModeBrowserTest {
 protected:
  // Hands Option+Cmd+Z to the window the way macOS hands it a shortcut. The
  // window needs no focus for that.
  void PressOptionCmdZ() {
    NSWindow* window =
        browser()->GetWindow()->GetNativeWindow().GetNativeNSWindow();
    [window performKeyEquivalent:cocoa_test_event_utils::SynthesizeKeyEvent(
                                     window, /*keyDown=*/true, ui::VKEY_Z,
                                     NSEventModifierFlagCommand |
                                         NSEventModifierFlagOption)];
  }
};

IN_PROC_BROWSER_TEST_F(UndoShortcutBrowserTest,
                       OptionCmdZUndoesOneOrganizationActionPerPress) {
  TabOrganizationController* controller =
      TabOrganizationController::From(browser());
  tabs::TabInterface* page = browser()->tab_strip_model()->GetTabAtIndex(0);
  const std::optional<PageId> id = controller->IdFor(page);
  ASSERT_TRUE(id);
  const std::optional<tab_groups::TabGroupId> task =
      controller->MoveToNewTask({*id}, u"Plan");
  ASSERT_TRUE(task);
  ASSERT_TRUE(controller->MoveToScratchpad({*id}));

  PressOptionCmdZ();
  ASSERT_TRUE(base::test::RunUntil([&] { return page->GetGroup() == task; }));
  EXPECT_TRUE(controller->CanUndo());

  PressOptionCmdZ();
  ASSERT_TRUE(base::test::RunUntil([&] { return !controller->CanUndo(); }));
  EXPECT_FALSE(page->GetGroup());

  PressOptionCmdZ();
  EXPECT_FALSE(page->GetGroup());
}

}  // namespace
}  // namespace uw
