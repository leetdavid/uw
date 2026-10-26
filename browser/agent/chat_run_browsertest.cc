// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/browser/agent/chat_run.h"

#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include "base/functional/bind.h"
#include "base/test/run_until.h"
#include "base/test/test_future.h"
#include "chrome/browser/actor/actor_keyed_service.h"
#include "chrome/browser/headless/test/headless_mode_browsertest.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface_iterator.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/test/browser_test.h"
#include "net/dns/mock_host_resolver.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "net/test/embedded_test_server/http_request.h"
#include "net/test/embedded_test_server/http_response.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "uw/browser/agent/run_window_mark.h"
#include "uw/browser/agent/scripted_model.h"

namespace uw {
namespace {

using ::testing::HasSubstr;
using Kind = ChatRun::Outcome::Kind;

class ChatRunBrowserTest : public headless::HeadlessModeBrowserTest {
 protected:
  void SetUpOnMainThread() override {
    HeadlessModeBrowserTest::SetUpOnMainThread();
    host_resolver()->AddRule("shop.test", "127.0.0.1");
    embedded_https_test_server().SetCertHostnames({"shop.test"});
    embedded_https_test_server().RegisterRequestHandler(base::BindRepeating(
        [](const net::test_server::HttpRequest& request)
            -> std::unique_ptr<net::test_server::HttpResponse> {
          if (request.relative_url != "/desk") {
            return nullptr;
          }
          auto response =
              std::make_unique<net::test_server::BasicHttpResponse>();
          response->set_content_type("text/html");
          response->set_content(
              "<!doctype html><title>Jarvis desk</title><p>Price: $479</p>");
          return response;
        }));
    ASSERT_TRUE(embedded_https_test_server().Start());
    ASSERT_TRUE(embedded_test_server()->Start());
  }

  void TearDownOnMainThread() override {
    run_.reset();
    actor::ActorKeyedService::Get(browser()->GetProfile())->ResetForTesting();
    HeadlessModeBrowserTest::TearDownOnMainThread();
  }

  std::string Url(std::string_view path) {
    return embedded_https_test_server().GetURL("shop.test", path).spec();
  }

  // Runs the chat's request with the scripted model and waits for the end.
  ChatRun::Outcome Run(std::string_view brief,
                       std::optional<ModelPrice> price = std::nullopt) {
    AgentModel model;
    model.connection.endpoint = embedded_test_server()->GetURL("/v1").spec();
    model.connection.model = "test-agent";
    model.api_key = "agent-key";
    model.price = price;
    base::test::TestFuture<ChatRun::Outcome> done;
    run_ = std::make_unique<ChatRun>(browser()->GetProfile(), std::move(model),
                                     ApprovalMode::kAuto, brief,
                                     done.GetCallback());
    return done.Take();
  }

  bool AnyRunWindow() {
    for (BrowserWindowInterface* window : GetAllBrowserWindowInterfaces()) {
      if (IsRunWindow(window)) {
        return true;
      }
    }
    return false;
  }

  ScriptedModel model_{*embedded_test_server()};
  std::unique_ptr<ChatRun> run_;
};

IN_PROC_BROWSER_TEST_F(ChatRunBrowserTest, ARunReadsThePageAndReports) {
  model_.Script(
      {ScriptedModel::Calls(base::ListValue().Append(ScriptedModel::Call(
           "open", base::DictValue().Set("url", Url("/desk"))))),
       ScriptedModel::Calls(base::ListValue().Append(
           ScriptedModel::Call("read", base::DictValue().Set("tab", 1)))),
       ScriptedModel::Calls(base::ListValue().Append(ScriptedModel::Call(
           "report", base::DictValue()
                         .Set("found", true)
                         .Set("message", "The desk is $479, below your $500.")
                         .Set("values", base::ListValue().Append(
                                            base::DictValue()
                                                .Set("name", "price")
                                                .Set("unit", "USD")
                                                .Set("number", 479)))))),
       ScriptedModel::Answer("Reported.")});

  const ChatRun::Outcome outcome =
      Run("The request: Tell me when the Jarvis desk drops below $500.");

  EXPECT_EQ(outcome.kind, Kind::kReported);
  EXPECT_EQ(outcome.report, (RunReport{true,
                                       "The desk is $479, below your $500.",
                                       {{"price", "USD", 479.0}}}));
  ASSERT_EQ(outcome.steps.size(), 4u);
  EXPECT_THAT(*outcome.steps[1].result.FindString("text"),
              HasSubstr("Price: $479"));
  const std::string first = model_.Requests().front();
  EXPECT_THAT(first, HasSubstr("Tell me when the Jarvis desk drops"));
  EXPECT_THAT(first, HasSubstr("\"name\":\"report\""));

  run_.reset();
  EXPECT_TRUE(base::test::RunUntil([&] { return !AnyRunWindow(); }));
}

IN_PROC_BROWSER_TEST_F(ChatRunBrowserTest, AWrongReportIsSentBackToTheModel) {
  model_.Script(
      {ScriptedModel::Calls(base::ListValue().Append(ScriptedModel::Call(
           "report", base::DictValue().Set("message", "Nothing new.")))),
       ScriptedModel::Calls(base::ListValue().Append(
           ScriptedModel::Call("report", base::DictValue()
                                             .Set("found", false)
                                             .Set("message", "Nothing new.")))),
       ScriptedModel::Answer("Reported.")});

  const ChatRun::Outcome outcome = Run("The request: Any news about uw?");

  EXPECT_EQ(outcome.kind, Kind::kReported);
  EXPECT_FALSE(outcome.report.found);
  EXPECT_EQ(*outcome.steps[0].result.FindString("error"),
            "report needs found, true or false.");
}

IN_PROC_BROWSER_TEST_F(ChatRunBrowserTest, ARunWithoutAReportFails) {
  model_.Script({ScriptedModel::Answer("I looked, but forgot to report.")});

  const ChatRun::Outcome outcome = Run("The request: Any news about uw?");

  EXPECT_EQ(outcome.kind, Kind::kFailed);
  EXPECT_EQ(outcome.error, "The run ended without a report.");
}

IN_PROC_BROWSER_TEST_F(ChatRunBrowserTest, ARunSaysWhatItsRequestsCost) {
  model_.Script({ScriptedModel::WithUsage(
      ScriptedModel::Answer("I looked, but forgot to report."), 500, 20)});

  const ChatRun::Outcome outcome = Run("The request: Any news about uw?",
                                       ModelPrice{.input = 1, .output = 2});

  EXPECT_EQ(outcome.usage.usage.input, 500);
  EXPECT_EQ(outcome.usage.usage.output, 20);
  EXPECT_DOUBLE_EQ(outcome.usage.cost, (500 * 1 + 20 * 2) / 1e6);
  EXPECT_TRUE(outcome.usage.priced);
}

IN_PROC_BROWSER_TEST_F(ChatRunBrowserTest, AModelThatCannotBeReachedIsNoFault) {
  model_.Script({ScriptedModel::Answer("unused")}, /*status=*/503);

  const ChatRun::Outcome outcome = Run("The request: Any news about uw?");

  EXPECT_EQ(outcome.kind, Kind::kModelUnreachable);
  EXPECT_THAT(outcome.error, HasSubstr("HTTP 503"));
}

}  // namespace
}  // namespace uw
