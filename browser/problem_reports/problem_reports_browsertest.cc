// Copyright 2026 The uw Authors. All rights reserved.

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "base/files/file_path.h"
#include "base/files/scoped_temp_dir.h"
#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/run_loop.h"
#include "base/strings/strcat.h"
#include "base/synchronization/lock.h"
#include "base/test/run_until.h"
#include "base/threading/thread_restrictions.h"
#include "base/values.h"
#include "chrome/browser/headless/test/headless_mode_browsertest.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/test/base/ui_test_utils.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "net/test/embedded_test_server/http_request.h"
#include "net/test/embedded_test_server/http_response.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/events/base_event_utils.h"
#include "ui/events/event.h"
#include "ui/views/controls/button/md_text_button.h"
#include "ui/views/test/button_test_api.h"
#include "ui/views/view_utils.h"
#include "url/gurl.h"
#include "uw/browser/problem_reports/answer_file.h"
#include "uw/browser/problem_reports/problem_reports.h"
#include "uw/browser/tab_organization/window_screenshot.h"
#include "uw/browser/ui/views/tab_organization/problem_reports_card.h"

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
  const until = (probe) => new Promise((resolve) => {
    const check = () => {
      const value = probe();
      if (value) {
        resolve(value);
      } else {
        setTimeout(check, 20);
      }
    };
    check();
  });
  const section = () => find(document, 'settings-uw-problem-reports-section');
  const part = (id) => section()?.shadowRoot?.getElementById(id) ?? null;
)";

ProblemReportsCard* FindCard(views::View* view) {
  if (auto* card = views::AsViewClass<ProblemReportsCard>(view)) {
    return card;
  }
  for (views::View* child : view->children()) {
    if (ProblemReportsCard* card = FindCard(child)) {
      return card;
    }
  }
  return nullptr;
}

// Plays a release whose reports go to PostHog's stand-in, the test server,
// with the answer kept in the test's own folder.
class ProblemReportsBrowserTest : public headless::HeadlessModeBrowserTest {
 protected:
  void SetUpOnMainThread() override {
    HeadlessModeBrowserTest::SetUpOnMainThread();
    embedded_test_server()->RegisterRequestHandler(base::BindRepeating(
        &ProblemReportsBrowserTest::Capture, base::Unretained(this)));
    ASSERT_TRUE(embedded_test_server()->Start());
    base::ScopedAllowBlockingForTesting blocking;
    ASSERT_TRUE(folder_.CreateUniqueTempDir());
    ProblemReports::Get().SetUpForTesting(ReleaseKeys(), AnswerFile());
  }

  void TearDownOnMainThread() override {
    ProblemReports::Get().SetSentCallbackForTesting(base::RepeatingClosure());
    HeadlessModeBrowserTest::TearDownOnMainThread();
  }

  void UseRelease() {
    ProblemReports::Get().SetUpForTesting(
        {.crash_url = "https://sentry.test/api/1/minidump/?sentry_key=k",
         .posthog_key = "phc_test",
         .posthog_host = embedded_test_server()->base_url().spec()},
        AnswerFile());
  }

  base::FilePath AnswerFile() { return folder_.GetPath().Append("answer"); }

  std::optional<ProblemReportsAnswer> SavedAnswer() {
    ProblemReports::Get().FlushForTesting();
    base::ScopedAllowBlockingForTesting blocking;
    return ReadProblemReportsAnswer(AnswerFile());
  }

  ProblemReportsCard* Card() {
    return FindCard(BrowserView::GetBrowserViewForBrowser(browser()));
  }

  static void Click(views::Button* button) {
    views::test::ButtonTestApi(button).NotifyClick(ui::MouseEvent(
        ui::EventType::kMousePressed, gfx::Point(), gfx::Point(),
        ui::EventTimeForNow(), ui::EF_LEFT_MOUSE_BUTTON, 0));
  }

  // Waits for the next report and reads it as PostHog would.
  base::DictValue NextReport() {
    EXPECT_TRUE(base::test::RunUntil([&] {
      base::AutoLock lock(lock_);
      return bodies_.size() > read_;
    }));
    base::AutoLock lock(lock_);
    std::optional<base::DictValue> report =
        base::JSONReader::ReadDict(bodies_[read_++], base::JSON_PARSE_RFC);
    EXPECT_TRUE(report);
    return report ? std::move(*report) : base::DictValue();
  }

  size_t Reports() {
    base::AutoLock lock(lock_);
    return bodies_.size();
  }

  content::EvalJsResult Run(const std::string& body) {
    return content::EvalJs(
        browser()->tab_strip_model()->GetActiveWebContents(),
        base::StrCat({"(async () => {", kHelpers, body, "})()"}));
  }

 private:
  std::unique_ptr<net::test_server::HttpResponse> Capture(
      const net::test_server::HttpRequest& request) {
    if (request.relative_url != "/i/v0/e/") {
      return nullptr;
    }
    {
      base::AutoLock lock(lock_);
      bodies_.push_back(request.content);
    }
    auto response = std::make_unique<net::test_server::BasicHttpResponse>();
    response->set_content(R"({"status":1})");
    response->set_content_type("application/json");
    return response;
  }

  base::ScopedTempDir folder_;
  base::Lock lock_;
  std::vector<std::string> bodies_;
  size_t read_ = 0;
};

IN_PROC_BROWSER_TEST_F(ProblemReportsBrowserTest, ADevelopmentBuildNeverAsks) {
  EXPECT_FALSE(ProblemReports::Get().available());
  EXPECT_FALSE(ProblemReports::Get().ShouldAsk());
  ASSERT_TRUE(Card());
  EXPECT_FALSE(Card()->GetVisible());
}

IN_PROC_BROWSER_TEST_F(ProblemReportsBrowserTest, AReleaseAsksUntilAnswered) {
  UseRelease();
  ASSERT_TRUE(Card());
  EXPECT_TRUE(Card()->GetVisible());
  EXPECT_EQ(Card()->send_button()->GetText(), u"Send reports");
  EXPECT_EQ(Card()->dont_send_button()->GetText(), u"Don't send");
  // Neither answer is the default.
  EXPECT_FALSE(Card()->send_button()->GetIsDefault());
  EXPECT_FALSE(Card()->dont_send_button()->GetIsDefault());
  SaveWindowScreenshot(browser(), "problem_reports_card");

  Click(Card()->send_button());
  EXPECT_FALSE(Card()->GetVisible());
  EXPECT_TRUE(ProblemReports::Get().sending());
  std::optional<ProblemReportsAnswer> answer = SavedAnswer();
  ASSERT_TRUE(answer);
  EXPECT_TRUE(answer->send);
  EXPECT_EQ(answer->install_id.size(), 36u);

  // The next launch reads the answer and does not ask again.
  UseRelease();
  EXPECT_FALSE(ProblemReports::Get().ShouldAsk());
  EXPECT_TRUE(ProblemReports::Get().sending());
}

IN_PROC_BROWSER_TEST_F(ProblemReportsBrowserTest, NoKeepsNoIdAndSendsNothing) {
  UseRelease();
  ProblemReports::Get().Answer(true);
  ProblemReports::Get().Answer(false);
  EXPECT_FALSE(Card()->GetVisible());
  std::optional<ProblemReportsAnswer> answer = SavedAnswer();
  ASSERT_TRUE(answer);
  EXPECT_FALSE(answer->send);
  EXPECT_TRUE(answer->install_id.empty());

  mojom::PageProblem problem;
  problem.kind = mojom::ProblemKind::kError;
  problem.message = "boom";
  ProblemReports::Get().ReportPageProblem(GURL("chrome://chat/"), problem);
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(Reports(), 0u);
}

IN_PROC_BROWSER_TEST_F(ProblemReportsBrowserTest, AnErrorOnAPageReachesPostHog) {
  UseRelease();
  ProblemReports::Get().Answer(true);
  ASSERT_TRUE(
      ui_test_utils::NavigateToURL(browser(), GURL("chrome://chat/?private")));
  ASSERT_TRUE(content::ExecJs(
      browser()->tab_strip_model()->GetActiveWebContents(),
      "setTimeout(() => { throw new TypeError('boom'); });"));

  base::DictValue report = NextReport();
  EXPECT_EQ(*report.FindString("api_key"), "phc_test");
  EXPECT_EQ(*report.FindString("event"), "$exception");
  EXPECT_EQ(*report.FindString("distinct_id"), SavedAnswer()->install_id);
  const base::DictValue* properties = report.FindDict("properties");
  ASSERT_TRUE(properties);
  // The address leaves out the query and the chat's ID.
  EXPECT_EQ(*properties->FindString("$current_url"), "chrome://chat/:id");
  const base::ListValue* exceptions = properties->FindList("$exception_list");
  ASSERT_TRUE(exceptions && !exceptions->empty());
  const base::DictValue& exception = (*exceptions)[0].GetDict();
  EXPECT_EQ(*exception.FindString("type"), "TypeError");
  EXPECT_EQ(*exception.FindString("value"), "boom");
}

IN_PROC_BROWSER_TEST_F(ProblemReportsBrowserTest, ARageClickNamesTheControl) {
  UseRelease();
  ProblemReports::Get().Answer(true);
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), GURL("chrome://chat/")));
  ASSERT_TRUE(content::ExecJs(
      browser()->tab_strip_model()->GetActiveWebContents(),
      "const button = document.createElement('button');"
      "button.id = 'stuck';"
      "button.setAttribute('aria-label', 'Retry');"
      "button.textContent = 'private words';"
      "document.body.append(button);"
      "for (let i = 0; i < 3; i++) {"
      "  button.dispatchEvent(new MouseEvent('click',"
      "      {bubbles: true, composed: true, clientX: 10, clientY: 10}));"
      "}"));

  base::DictValue report = NextReport();
  EXPECT_EQ(*report.FindString("event"), "$rageclick");
  const base::DictValue* properties = report.FindDict("properties");
  ASSERT_TRUE(properties);
  EXPECT_EQ(*properties->FindString("element_tag"), "button");
  EXPECT_EQ(*properties->FindString("element_id"), "stuck");
  EXPECT_EQ(*properties->FindString("element_label"), "Retry");
  // What the control shows never leaves the page.
  EXPECT_EQ(report.DebugString().find("private words"), std::string::npos);
}

IN_PROC_BROWSER_TEST_F(ProblemReportsBrowserTest,
                       TheSettingsSwitchHoldsTheAnswer) {
  UseRelease();
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(),
                                           GURL("chrome://settings/privacy")));
  ASSERT_EQ(true, Run("await until(() => part('sendToggle'));"
                      "return !part('sendToggle').checked;"));
  EXPECT_EQ(true, Run("return !!part('reportIssue');"));
  SaveWindowScreenshot(browser(), "problem_reports_settings");

  ASSERT_EQ(true, Run("part('sendToggle').click(); return true;"));
  EXPECT_TRUE(base::test::RunUntil(
      [] { return ProblemReports::Get().sending(); }));
  EXPECT_FALSE(Card()->GetVisible());
  EXPECT_TRUE(SavedAnswer()->send);

  ASSERT_EQ(true, Run("part('sendToggle').click(); return true;"));
  EXPECT_TRUE(base::test::RunUntil(
      [] { return !ProblemReports::Get().sending(); }));
}

IN_PROC_BROWSER_TEST_F(ProblemReportsBrowserTest,
                       ADevelopmentBuildShowsNoSwitch) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(),
                                           GURL("chrome://settings/privacy")));
  ASSERT_EQ(true, Run("await until(() => part('reportIssue'));"
                      "return !part('sendToggle');"));
}

}  // namespace
}  // namespace uw
