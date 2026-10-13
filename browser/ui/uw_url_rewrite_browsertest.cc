// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/browser/ui/uw_url_rewrite.h"

#include "chrome/browser/headless/test/headless_mode_browsertest.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/test/base/ui_test_utils.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/url_constants.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "content/public/test/test_navigation_observer.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "url/gurl.h"

namespace uw {
namespace {

// The address of the page a tab shows, which can differ from the address in
// its address bar.
GURL PageAddress(content::WebContents* contents) {
  return contents->GetPrimaryMainFrame()->GetLastCommittedURL();
}

class UwSchemeBrowserTest : public headless::HeadlessModeBrowserTest {
 protected:
  content::WebContents* contents() {
    return browser()->tab_strip_model()->GetActiveWebContents();
  }
};

IN_PROC_BROWSER_TEST_F(UwSchemeBrowserTest,
                       AnAddressTypedInTheAddressBarOpensItsPage) {
  content::TestNavigationObserver navigation(contents());
  ui_test_utils::SendToOmniboxAndSubmit(browser(), "uw://settings");
  navigation.Wait();

  EXPECT_EQ(PageAddress(contents()), GURL("chrome://settings/"));
}

IN_PROC_BROWSER_TEST_F(UwSchemeBrowserTest,
                       AnAddressOpenedByTheBrowserOpensItsPage) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), GURL("uw://settings")));

  EXPECT_EQ(PageAddress(contents()), GURL("chrome://settings/"));
}

// A web page may not open chrome:// pages, so it may not open them by their
// uw:// names either.
IN_PROC_BROWSER_TEST_F(UwSchemeBrowserTest, AWebPageCannotOpenAnAddress) {
  ASSERT_TRUE(embedded_test_server()->Start());
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("/title1.html")));

  ui_test_utils::TabAddedWaiter new_tab(browser());
  ASSERT_TRUE(content::ExecJs(
      contents(), "window.open('uw://version', '_blank', 'noopener')"));
  content::WebContents* opened = new_tab.Wait();
  content::WaitForLoadStop(opened);

  EXPECT_FALSE(PageAddress(opened).SchemeIs(content::kChromeUIScheme))
      << PageAddress(opened);
}

}  // namespace
}  // namespace uw
