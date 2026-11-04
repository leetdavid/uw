// Copyright 2026 The uw Authors. All rights reserved.

#include "base/test/run_until.h"
#include "chrome/app/chrome_command_ids.h"
#include "chrome/browser/headless/test/headless_mode_browsertest.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/common/webui_url_constants.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "ui/base/accelerators/accelerator.h"
#include "ui/events/event_constants.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/views/focus/focus_manager.h"

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

}  // namespace
}  // namespace uw
