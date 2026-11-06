// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/browser/little_window/little_window.h"

#import <AppKit/AppKit.h>

#include <vector>

#include "base/run_loop.h"
#include "base/task/sequenced_task_runner.h"
#include "base/test/run_until.h"
#include "base/time/time.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/headless/test/headless_mode_browsertest.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface_iterator.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/prefs/pref_service.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "uw/browser/little_window/little_window_key.h"
#include "uw/browser/little_window/little_window_mark.h"
#include "uw/browser/little_window/little_window_prefs.h"
#include "uw/browser/tab_organization/window_screenshot.h"
#include "uw/browser/ui/views/command_bar/command_bar.h"

namespace uw {
namespace {

// Little windows in a real window on the Mac. Windows' are
// LittleWindowWinBrowserTest. See docs/plans/little-window.md.
class LittleWindowMacBrowserTest : public headless::HeadlessModeBrowserTest {
 protected:
  // The little windows open now, the one used last first.
  std::vector<BrowserWindowInterface*> Littles() {
    std::vector<BrowserWindowInterface*> found;
    ForEachCurrentBrowserWindowInterfaceOrderedByActivation(
        [&](BrowserWindowInterface* window) {
          if (IsLittleWindow(window)) {
            found.push_back(window);
          }
          return true;
        });
    return found;
  }

  static NSWindow* Native(BrowserWindowInterface* window) {
    return window->GetWindow()->GetNativeWindow().GetNativeNSWindow();
  }

  // macOS lets no test press a key in another app, as Chromium's
  // GlobalCommandsApiTest says, so this starts where macOS hands uw the key.
  static void PressTheKey() {
    LittleWindowKeyListener::Get()->OnKeyPressed(*LittleWindowKey());
  }

  void Wait(base::TimeDelta delay) {
    base::RunLoop loop;
    base::SequencedTaskRunner::GetCurrentDefault()->PostDelayedTask(
        FROM_HERE, loop.QuitClosure(), delay);
    loop.Run();
  }
};

// Run with pnpm run window-tests. macOS's window buttons sit centered on the
// toolbar, per check 3.
IN_PROC_BROWSER_TEST_F(LittleWindowMacBrowserTest,
                       MANUAL_ItLooksLikeARegularWindowWithoutTheSidebar) {
  ASSERT_TRUE(embedded_test_server()->Start());
  BrowserWindowInterface* little =
      OpenLittleWindow(embedded_test_server()->GetURL("/title1.html"));
  ASSERT_TRUE(little);
  ASSERT_TRUE(content::WaitForLoadStop(
      little->GetTabStripModel()->GetActiveWebContents()));
  ASSERT_TRUE(base::test::RunUntil([&] { return Native(little).isVisible; }));
  Wait(base::Milliseconds(500));
  SaveNativeWindowScreenshot(little->GetBrowserForMigrationOnly(),
                             "little-window-real");
}

// Run with pnpm run window-tests, per check 9. With Finder in front, the key
// brings uw forward with a little window and the command bar, on the Space in
// view. With uw's window full screen, the next one opens over it, on that
// Space. Each switch turns its half off.
IN_PROC_BROWSER_TEST_F(LittleWindowMacBrowserTest,
                       MANUAL_TheKeyFromAnotherAppOpensOneOnTheSpaceInView) {
  LittleWindowKeyListener::ListenInTestsForTesting();
  ASSERT_TRUE(LittleWindowKeyListener::Get()->listening());

  NSRunningApplication* finder =
      [NSRunningApplication
          runningApplicationsWithBundleIdentifier:@"com.apple.finder"]
          .firstObject;
  ASSERT_TRUE(finder);
  [finder activateWithOptions:0];
  ASSERT_TRUE(base::test::RunUntil([] { return !NSApp.isActive; }));

  PressTheKey();
  ASSERT_TRUE(base::test::RunUntil([&] { return Littles().size() == 1; }));
  BrowserWindowInterface* little = Littles()[0];
  EXPECT_TRUE(base::test::RunUntil([&] {
    return NSApp.isActive && Native(little).isVisible &&
           Native(little).isOnActiveSpace;
  }));
  EXPECT_TRUE(base::test::RunUntil(
      [&] { return CommandBar::From(little)->IsShowing(); }));
  SaveNativeWindowScreenshot(little->GetBrowserForMigrationOnly(),
                             "little-window-from-another-app");

  PrefService* prefs = g_browser_process->local_state();
  prefs->SetBoolean(kLittleWindowKeyInUwPref, false);
  PressTheKey();
  Wait(base::Milliseconds(500));
  EXPECT_EQ(1u, Littles().size());
  prefs->SetBoolean(kLittleWindowKeyInUwPref, true);

  ui_test_utils::ToggleFullscreenModeAndWait(browser());
  ASSERT_TRUE(
      base::test::RunUntil([&] { return Native(browser()).isOnActiveSpace; }));
  PressTheKey();
  ASSERT_TRUE(base::test::RunUntil([&] { return Littles().size() == 2; }));
  BrowserWindowInterface* over =
      Littles()[0] == little ? Littles()[1] : Littles()[0];
  EXPECT_TRUE(base::test::RunUntil(
      [&] { return Native(over).isVisible && Native(over).isOnActiveSpace; }));
  // Long enough for macOS to have moved to another Space, had it done so.
  Wait(base::Seconds(1));
  EXPECT_TRUE(Native(over).isOnActiveSpace);
  EXPECT_TRUE(Native(browser()).isOnActiveSpace);
  EXPECT_TRUE(browser()->GetWindow()->IsFullscreen());

  prefs->SetBoolean(kLittleWindowKeyFromAppsPref, false);
  EXPECT_FALSE(LittleWindowKeyListener::Get()->listening());
  ui_test_utils::ToggleFullscreenModeAndWait(browser());
}

}  // namespace
}  // namespace uw
