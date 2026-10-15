// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/browser/ui/views/command_bar/command_bar.h"

#import <AppKit/AppKit.h>

#include <string>

#include "base/run_loop.h"
#include "base/task/sequenced_task_runner.h"
#include "base/time/time.h"
#include "chrome/browser/headless/test/headless_mode_browsertest.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "components/prefs/pref_service.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "ui/events/keycodes/keyboard_codes.h"
#import "ui/events/test/cocoa_test_event_utils.h"
#include "uw/browser/tab_organization/ai_connection.h"

namespace uw {
namespace {

class CommandBarBrowserTest : public headless::HeadlessModeBrowserTest {
 protected:
  CommandBar* bar() { return CommandBar::From(browser()); }

  NSWindow* window() {
    return browser()->GetWindow()->GetNativeWindow().GetNativeNSWindow();
  }

  // Hands a key to the window the way macOS hands it a shortcut.
  void Press(ui::KeyboardCode key, NSEventModifierFlags modifiers) {
    [window()
        performKeyEquivalent:cocoa_test_event_utils::SynthesizeKeyEvent(
                                 window(), /*keyDown=*/true, key, modifiers)];
  }

  // Queues a key behind the ones already waiting, as fast typing does.
  void Queue(ui::KeyboardCode key, NSEventModifierFlags modifiers) {
    for (bool down : {true, false}) {
      [NSApp postEvent:cocoa_test_event_utils::SynthesizeKeyEvent(
                           window(), down, key, modifiers)
               atStart:NO];
    }
  }

  std::string Field() {
    return content::EvalJs(bar()->page(),
                           "document.querySelector('#field').value")
        .ExtractString();
  }

  int Rows() {
    return content::EvalJs(bar()->page(),
                           "document.querySelectorAll('#results li').length")
        .ExtractInt();
  }

  void Wait(base::TimeDelta delay) {
    base::RunLoop loop;
    base::SequencedTaskRunner::GetCurrentDefault()->PostDelayedTask(
        FROM_HERE, loop.QuitClosure(), delay);
    loop.Run();
  }

  // Asks the page every 20 ms for up to two seconds.
  template <typename Condition>
  bool Eventually(Condition condition) {
    for (int i = 0; i < 100; ++i) {
      if (condition()) {
        return true;
      }
      Wait(base::Milliseconds(20));
    }
    return false;
  }
};

IN_PROC_BROWSER_TEST_F(CommandBarBrowserTest, CmdTOpensItWithoutATab) {
  ASSERT_TRUE(bar()->page());
  const int tabs = browser()->tab_strip_model()->count();

  Press(ui::VKEY_T, NSEventModifierFlagCommand);

  EXPECT_TRUE(bar()->IsShowing());
  EXPECT_EQ(tabs, browser()->tab_strip_model()->count());
}

IN_PROC_BROWSER_TEST_F(CommandBarBrowserTest, TurnedOffCmdTOpensATab) {
  browser()->GetProfile()->GetPrefs()->SetBoolean(kCommandBarEnabledPref,
                                                  false);
  EXPECT_FALSE(bar()->page());
  const int tabs = browser()->tab_strip_model()->count();

  Press(ui::VKEY_T, NSEventModifierFlagCommand);

  EXPECT_FALSE(bar()->IsShowing());
  EXPECT_EQ(tabs + 1, browser()->tab_strip_model()->count());
}

IN_PROC_BROWSER_TEST_F(CommandBarBrowserTest, TurningItOffClosesIt) {
  Press(ui::VKEY_T, NSEventModifierFlagCommand);
  ASSERT_TRUE(bar()->IsShowing());

  browser()->GetProfile()->GetPrefs()->SetBoolean(kCommandBarEnabledPref,
                                                  false);

  EXPECT_FALSE(bar()->IsShowing());
  EXPECT_FALSE(bar()->page());
}

// The keyboard tests need a real window: a headless panel never takes focus,
// so its page drops the keys. Run them with --run-manual --headful-mode.
IN_PROC_BROWSER_TEST_F(CommandBarBrowserTest,
                       MANUAL_EscapeClosesItAndOpensNothing) {
  const int tabs = browser()->tab_strip_model()->count();
  Press(ui::VKEY_T, NSEventModifierFlagCommand);
  ASSERT_TRUE(bar()->IsShowing());

  Queue(ui::VKEY_ESCAPE, 0);

  EXPECT_TRUE(Eventually([&] { return !bar()->IsShowing(); }));
  EXPECT_EQ(tabs, browser()->tab_strip_model()->count());
}

// Keys typed right after Cmd+T, before the panel has drawn, land in its field
// in order.
IN_PROC_BROWSER_TEST_F(CommandBarBrowserTest,
                       MANUAL_KeysTypedRightAfterCmdTLand) {
  ASSERT_TRUE(content::WaitForLoadStop(bar()->page()));

  Queue(ui::VKEY_T, NSEventModifierFlagCommand);
  for (ui::KeyboardCode key : {ui::VKEY_G, ui::VKEY_I, ui::VKEY_T, ui::VKEY_H,
                               ui::VKEY_U, ui::VKEY_B}) {
    Queue(key, 0);
  }

  EXPECT_TRUE(Eventually([&] { return Field() == "github"; }))
      << "The field holds: " << Field();
}

IN_PROC_BROWSER_TEST_F(CommandBarBrowserTest, TypingListsSuggestions) {
  ASSERT_TRUE(content::WaitForLoadStop(bar()->page()));
  Press(ui::VKEY_T, NSEventModifierFlagCommand);

  ASSERT_TRUE(content::ExecJs(bar()->page(), R"(
      const field = document.querySelector('#field');
      field.value = 'uw browser';
      field.dispatchEvent(new Event('input'));
  )"));

  EXPECT_TRUE(Eventually([&] { return Rows() > 0; }));
}

}  // namespace
}  // namespace uw
