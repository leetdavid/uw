// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/browser/problem_reports/webui_error_reports.h"

#include <utility>
#include <vector>

#include "base/command_line.h"
#include "base/run_loop.h"
#include "base/test/bind.h"
#include "base/test/run_until.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/headless/test/headless_mode_browsertest.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/crash/content/browser/error_reporting/javascript_error_report.h"
#include "components/crash/content/browser/error_reporting/js_error_report_processor.h"
#include "components/prefs/pref_service.h"
#include "components/webui/chrome_urls/pref_names.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace uw {
namespace {

using ::testing::HasSubstr;
using ::testing::Not;

// Chromium's switch is what a preview sets, so these tests see a preview.
class WebUIErrorReportsBrowserTest : public headless::HeadlessModeBrowserTest {
 protected:
  void SetUpCommandLine(base::CommandLine* command_line) override {
    HeadlessModeBrowserTest::SetUpCommandLine(command_line);
    command_line->AppendSwitch("disable-crash-on-webui-js-error");
  }

  void SetUpOnMainThread() override {
    HeadlessModeBrowserTest::SetUpOnMainThread();
    // chrome://omnibox is one of Chromium's debug pages.
    g_browser_process->local_state()->SetBoolean(
        chrome_urls::kInternalOnlyUisEnabled, true);
    SetWebUIErrorReportCallbackForTesting(
        base::BindLambdaForTesting([this](const WebUIErrorReport& report) {
          reports_.push_back(report);
        }));
  }

  void TearDownOnMainThread() override {
    SetWebUIErrorReportCallbackForTesting({});
    HeadlessModeBrowserTest::TearDownOnMainThread();
  }

  std::vector<WebUIErrorReport> reports_;
};

// chrome://omnibox asks development builds to crash on a JavaScript error, as
// the address bar's list does. A preview carries on and reports it, redacted.
IN_PROC_BROWSER_TEST_F(WebUIErrorReportsBrowserTest,
                       AnErrorOnAPageThatAsksToCrashIsReported) {
  ASSERT_TRUE(
      ui_test_utils::NavigateToURL(browser(), GURL("chrome://omnibox/")));
  content::WebContents* page =
      browser()->tab_strip_model()->GetActiveWebContents();
  // The page's own function, called without its element, fails with the text
  // it was given in the error's message.
  ASSERT_TRUE(content::ExecJs(page, R"(
      setTimeout(
          customElements.get('omnibox-output').prototype
              .updateSelectedResponseIndex,
          0, 'me@example.com https://example.com/private');
  )"));

  ASSERT_TRUE(base::test::RunUntil([&] { return !reports_.empty(); }));
  EXPECT_EQ("chrome://omnibox/", reports_[0].page);
  EXPECT_THAT(reports_[0].source, HasSubstr("chrome://omnibox/"));
  EXPECT_THAT(reports_[0].message,
              HasSubstr("Cannot read properties of undefined"));
  EXPECT_THAT(reports_[0].message, Not(HasSubstr("me@example.com")));
  EXPECT_THAT(reports_[0].message, Not(HasSubstr("example.com/private")));
  EXPECT_THAT(reports_[0].stack, HasSubstr("updateSelectedResponseIndex"));
  EXPECT_THAT(reports_[0].stack, Not(HasSubstr("me@example.com")));
  EXPECT_EQ(2, content::EvalJs(page, "1 + 1"));
}

// A page that doesn't ask to crash, such as chrome://version, sends nothing.
IN_PROC_BROWSER_TEST_F(WebUIErrorReportsBrowserTest, OtherPagesSendNothing) {
  JavaScriptErrorReport error;
  error.message = "An error on chrome://version";
  error.url = "chrome://version/version.js";
  error.page_url = "chrome://version/";
  base::RunLoop sent;
  JsErrorReportProcessor::Get()->SendErrorReport(
      std::move(error), sent.QuitClosure(), browser()->GetProfile());
  sent.Run();
  EXPECT_TRUE(reports_.empty());
}

}  // namespace
}  // namespace uw
