// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/browser/tab_organization/window_screenshot.h"

#include <string>

#include "base/run_loop.h"
#include "chrome/browser/headless/test/headless_mode_browsertest.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/navigator/browser_navigator.h"
#include "chrome/browser/ui/navigator/browser_navigator_params.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "url/gurl.h"
#include "uw/browser/tab_organization/tab_organization_controller.h"

namespace uw {
namespace {

// Stages the sidebar with nested tabs and one task for a screenshot.
class WindowScreenshotBrowserTest : public headless::HeadlessModeBrowserTest {
 protected:
  tabs::TabInterface* OpenLinkFrom(tabs::TabInterface* source,
                                   const std::string& title) {
    NavigateParams params(browser(),
                          GURL("data:text/html,<title>" + title + "</title>"),
                          ui::PAGE_TRANSITION_LINK);
    params.disposition = WindowOpenDisposition::NEW_BACKGROUND_TAB;
    params.source_contents = source->GetContents();
    Navigate(&params);
    EXPECT_TRUE(
        content::WaitForLoadStop(params.navigated_or_inserted_contents));
    base::RunLoop().RunUntilIdle();
    return browser()->tab_strip_model()->GetTabForWebContents(
        params.navigated_or_inserted_contents);
  }
};

IN_PROC_BROWSER_TEST_F(WindowScreenshotBrowserTest, Sidebar) {
  TabOrganizationController* controller =
      TabOrganizationController::From(browser());
  tabs::TabInterface* first = browser()->tab_strip_model()->GetTabAtIndex(0);
  tabs::TabInterface* flights = OpenLinkFrom(first, "SFO to LIS flights");
  OpenLinkFrom(flights, "Fare details: TAP Air Portugal");
  tabs::TabInterface* news = OpenLinkFrom(first, "Hacker News");
  OpenLinkFrom(news, "Show HN: Tree-style tabs for Chromium");
  ASSERT_TRUE(controller->MoveToNewTask({*controller->IdFor(flights)},
                                        u"Plan Lisbon trip"));
  EXPECT_EQ(controller->tree().Tasks().size(), 1u);
  SaveWindowScreenshot(browser(), "sidebar");
}

}  // namespace
}  // namespace uw
