// Copyright 2026 The uw Authors. All rights reserved.

#include <windows.h>

#include <vector>

#include "base/test/run_until.h"
#include "chrome/browser/headless/test/headless_mode_browsertest.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface_iterator.h"
#include "content/public/test/browser_test.h"
#include "uw/browser/little_window/little_window_key.h"
#include "uw/browser/little_window/little_window_mark.h"
#include "uw/browser/tab_organization/window_screenshot.h"
#include "uw/browser/ui/views/command_bar/command_bar.h"

namespace uw {
namespace {

// The little window's key pressed in another app on Windows. The Mac's are
// LittleWindowMacBrowserTest. See docs/plans/little-window.md#windows.
class LittleWindowWinBrowserTest : public headless::HeadlessModeBrowserTest {
 protected:
  // Browser tests on Windows draw nothing unless asked to.
  LittleWindowWinBrowserTest() { EnablePixelOutput(); }

  BrowserWindowInterface* Little() {
    BrowserWindowInterface* found = nullptr;
    ForEachCurrentBrowserWindowInterfaceOrderedByActivation(
        [&](BrowserWindowInterface* window) {
          if (IsLittleWindow(window)) {
            found = window;
            return false;
          }
          return true;
        });
    return found;
  }

  static INPUT Key(WORD key, bool down) {
    INPUT input = {.type = INPUT_KEYBOARD};
    input.ki.wVk = key;
    input.ki.dwFlags = down ? 0 : KEYEVENTF_KEYUP;
    return input;
  }

  // Ctrl+Alt+N as the keyboard sends it.
  void PressTheKey() {
    std::vector<INPUT> inputs = {Key(VK_CONTROL, true), Key(VK_MENU, true),
                                 Key('N', true),        Key('N', false),
                                 Key(VK_MENU, false),   Key(VK_CONTROL, false)};
    ASSERT_EQ(::SendInput(inputs.size(), inputs.data(), sizeof(INPUT)),
              inputs.size());
  }
};

// Run in the Windows VM with --run-manual --headful-mode, per check 13. With
// uw's window minimized, Ctrl+Alt+N opens a little window in front with the
// command bar. A keyboard that types "ń" with AltGr+N leaves the key to other
// apps.
IN_PROC_BROWSER_TEST_F(LittleWindowWinBrowserTest,
                       MANUAL_CtrlAltNFromAnotherAppAndAPolishKeyboard) {
  LittleWindowKeyListener::ListenInTestsForTesting();
  LittleWindowKeyListener* listener = LittleWindowKeyListener::Get();
  ASSERT_TRUE(listener->listening());

  browser()->GetWindow()->Minimize();
  ASSERT_TRUE(base::test::RunUntil([] { return !internal::IsUwInFront(); }));

  PressTheKey();
  ASSERT_TRUE(base::test::RunUntil([&] { return Little(); }));
  BrowserWindowInterface* little = Little();
  EXPECT_TRUE(base::test::RunUntil([] { return internal::IsUwInFront(); }));
  EXPECT_TRUE(base::test::RunUntil(
      [&] { return CommandBar::From(little)->IsShowing(); }));
  SaveWindowScreenshot(little->GetBrowserForMigrationOnly(),
                       "little-window-from-another-app-windows");

  // Polish (Programmers) types "ń" with AltGr+N.
  HKL before = ::GetKeyboardLayout(0);
  HKL polish = ::LoadKeyboardLayoutW(L"00000415", KLF_ACTIVATE);
  ASSERT_TRUE(polish);
  listener->Update();
  EXPECT_EQ(u"\u0144", listener->altgr_character());
  EXPECT_FALSE(listener->listening());

  ::ActivateKeyboardLayout(before, 0);
  if (polish != before) {
    ::UnloadKeyboardLayout(polish);
  }
  listener->Update();
  EXPECT_TRUE(listener->listening());
}

}  // namespace
}  // namespace uw
