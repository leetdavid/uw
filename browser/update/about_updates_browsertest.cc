// Copyright 2026 The uw Authors. All rights reserved.

#include <string>

#include "base/functional/bind.h"
#include "base/strings/strcat.h"
#include "chrome/browser/headless/test/headless_mode_browsertest.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/test/base/ui_test_utils.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"
#include "uw/browser/update/update_check.h"

namespace uw {
namespace {

constexpr char kHelpers[] = R"(
  const find = (root, selector) => {
    const found = root.querySelector(selector);
    if (found) {
      return found;
    }
    for (const element of root.querySelectorAll('*')) {
      const inner = element.shadowRoot && find(element.shadowRoot, selector);
      if (inner) {
        return inner;
      }
    }
    return null;
  };
  const settle = (probe) => new Promise((resolve) => {
    const deadline = Date.now() + 10000;
    const check = () => {
      if (probe() || Date.now() > deadline) {
        resolve(true);
      } else {
        setTimeout(check, 20);
      }
    };
    check();
  });
  const page = () => find(document, 'settings-about-page');
  const part = (id) => page()?.shadowRoot?.getElementById(id) ?? null;
  const message = () => part('updateStatusMessage')
      ?.querySelector('[role=alert]')?.textContent.trim() ?? '';
  const learnMore = () => part('updateStatusMessage')?.querySelector('a');
)";

// About uw with a stand-in for uw's updater, which counts the checks it is
// asked for.
class AboutUpdatesBrowserTest : public headless::HeadlessModeBrowserTest {
 protected:
  void TearDownOnMainThread() override {
    ResetUpdateCheckForTesting();
    HeadlessModeBrowserTest::TearDownOnMainThread();
  }

  void UseUpdater() {
    SetUpdateChecker(base::BindRepeating(
        [](int* checks) {
          ++*checks;
          ReportUpdateState(UpdateState::kChecking);
        },
        &checks_));
  }

  void OpenAbout() {
    ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(),
                                             GURL("chrome://settings/help")));
  }

  content::EvalJsResult Run(const std::string& body) {
    return content::EvalJs(
        browser()->tab_strip_model()->GetActiveWebContents(),
        base::StrCat({"(async () => {", kHelpers, body, "})()"}));
  }

  // What About uw says once it says `text`, or after ten seconds.
  std::string Says(const std::string& text) {
    return Run(content::JsReplace(
                   "await settle(() => message() === $1); return message();",
                   text))
        .ExtractString();
  }

  int checks_ = 0;
};

IN_PROC_BROWSER_TEST_F(AboutUpdatesBrowserTest, ADailyBuildShowsNoUpdateLine) {
  OpenAbout();

  ASSERT_EQ(true,
            Run("await settle(() => page() &&"
                "    'rollback' in page().currentUpdateStatusEvent_);"
                "return 'rollback' in page().currentUpdateStatusEvent_;"));
  EXPECT_EQ(true, Run("return part('updateStatusMessage').hidden;"));
}

IN_PROC_BROWSER_TEST_F(AboutUpdatesBrowserTest, OpeningAboutChecksAtOnce) {
  UseUpdater();
  OpenAbout();

  EXPECT_EQ(Says("Checking for updates"), "Checking for updates");
  EXPECT_EQ(checks_, 1);
}

IN_PROC_BROWSER_TEST_F(AboutUpdatesBrowserTest, AboutShowsWhatTheUpdaterDoes) {
  UseUpdater();
  OpenAbout();
  ASSERT_EQ(Says("Checking for updates"), "Checking for updates");

  ReportUpdateState(UpdateState::kDownloading);
  EXPECT_EQ(Says("Updating uw"), "Updating uw");

  ReportUpdateState(UpdateState::kUpToDate);
  EXPECT_EQ(Says("uw is up to date"), "uw is up to date");
  EXPECT_EQ(true, Run("return part('relaunch').hidden;"));

  ReportUpdateState(UpdateState::kReady);
  EXPECT_EQ(Says("Nearly up to date! Relaunch uw to finish updating."),
            "Nearly up to date! Relaunch uw to finish updating.");
  EXPECT_EQ(false, Run("return part('relaunch').hidden;"));
}

IN_PROC_BROWSER_TEST_F(AboutUpdatesBrowserTest, AFailedCheckLinksToUwsHelp) {
  UseUpdater();
  OpenAbout();
  ASSERT_EQ(Says("Checking for updates"), "Checking for updates");
  EXPECT_EQ(true, Run("return learnMore().hidden;"));

  ReportUpdateState(UpdateState::kFailed);

  EXPECT_EQ(Says("uw couldn't check for updates. Check your connection, then "
                 "open this page again."),
            "uw couldn't check for updates. Check your connection, then open "
            "this page again.");
  EXPECT_EQ(false, Run("return learnMore().hidden;"));
  EXPECT_EQ("https://uwbrowser.com/help/install-uw#updates-come-by-themselves",
            Run("return learnMore().href;"));
}

IN_PROC_BROWSER_TEST_F(AboutUpdatesBrowserTest, ACheckUnderWayShowsItsState) {
  SetUpdateChecker(
      base::BindRepeating([](int* checks) { ++*checks; }, &checks_));
  ReportUpdateState(UpdateState::kDownloading);

  OpenAbout();

  EXPECT_EQ(Says("Updating uw"), "Updating uw");
  EXPECT_EQ(checks_, 1);
}

}  // namespace
}  // namespace uw
