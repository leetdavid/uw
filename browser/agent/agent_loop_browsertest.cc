// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/browser/agent/agent_loop.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/test/run_until.h"
#include "chrome/browser/actor/actor_keyed_service.h"
#include "chrome/browser/headless/test/headless_mode_browsertest.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "net/dns/mock_host_resolver.h"
#include "net/http/http_status_code.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "net/test/embedded_test_server/http_request.h"
#include "net/test/embedded_test_server/http_response.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "uw/browser/agent/scripted_model.h"

namespace uw {
namespace {

using ::testing::HasSubstr;
using ::testing::Not;
using Kind = AgentLoop::Step::Kind;

constexpr char kForm[] = R"(<!doctype html>
<title>Book a tour</title>
<label for="name">Your name</label>
<input id="name" name="name">
<button onclick="document.getElementById('said').textContent =
    document.getElementById('name').value">Send</button>
<p id="said"></p>)";

class AgentLoopBrowserTest : public headless::HeadlessModeBrowserTest {
 protected:
  void SetUpOnMainThread() override {
    HeadlessModeBrowserTest::SetUpOnMainThread();
    host_resolver()->AddRule("shop.test", "127.0.0.1");
    embedded_https_test_server().SetCertHostnames({"shop.test"});
    embedded_https_test_server().RegisterRequestHandler(base::BindRepeating(
        [](const net::test_server::HttpRequest& request)
            -> std::unique_ptr<net::test_server::HttpResponse> {
          if (request.relative_url != "/form") {
            return nullptr;
          }
          auto response =
              std::make_unique<net::test_server::BasicHttpResponse>();
          response->set_content_type("text/html");
          response->set_content(kForm);
          return response;
        }));
    ASSERT_TRUE(embedded_https_test_server().Start());
    ASSERT_TRUE(embedded_test_server()->Start());
    chat_ = browser()->tab_strip_model()->GetActiveTab();
    agent_ = std::make_unique<AgentLoop>(
        *chat_,
        AgentModel{embedded_test_server()->GetURL("/v1/chat/completions"),
                   "test-agent", "agent-key"},
        base::BindRepeating(&AgentLoopBrowserTest::OnStep,
                            base::Unretained(this)));
  }

  void TearDownOnMainThread() override {
    agent_.reset();
    actor::ActorKeyedService::Get(browser()->GetProfile())->ResetForTesting();
    HeadlessModeBrowserTest::TearDownOnMainThread();
  }

  // Sends `text` and waits until the agent answers or gives up.
  const AgentLoop::Step& Ask(std::string_view text) {
    steps_.clear();
    agent_->Send(text);
    EXPECT_TRUE(base::test::RunUntil([&] { return !agent_->working(); }));
    return steps_.back();
  }

  std::string Url(std::string_view path) {
    return embedded_https_test_server().GetURL("shop.test", path).spec();
  }

  ScriptedModel model_{*embedded_test_server()};
  std::vector<AgentLoop::Step> steps_;
  raw_ptr<tabs::TabInterface> chat_;
  std::unique_ptr<AgentLoop> agent_;

 private:
  void OnStep(const AgentLoop::Step& step) {
    AgentLoop::Step copy;
    copy.kind = step.kind;
    copy.tool = step.tool;
    copy.args = step.args.Clone();
    copy.result = step.result.Clone();
    copy.text = step.text;
    steps_.push_back(std::move(copy));
  }
};

IN_PROC_BROWSER_TEST_F(AgentLoopBrowserTest, FillsAFormAndSaysItIsDone) {
  model_.Script(
      {ScriptedModel::Calls(base::ListValue().Append(ScriptedModel::Call(
           "open", base::DictValue().Set("url", Url("/form"))))),
       ScriptedModel::Calls(base::ListValue().Append(
           ScriptedModel::Call("read", base::DictValue().Set("tab", 1)))),
       ScriptedModel::Calls(
           base::ListValue()
               .Append(ScriptedModel::Call("type", base::DictValue()
                                                       .Set("tab", 1)
                                                       .Set("element", 1)
                                                       .Set("text", "Ada")))
               .Append(ScriptedModel::Call(
                   "click",
                   base::DictValue().Set("tab", 1).Set("element", 2)))),
       ScriptedModel::Answer("Booked the tour for Ada.")});

  const AgentLoop::Step& end = Ask("Book a tour for Ada");

  EXPECT_EQ(end.kind, Kind::kAnswer);
  EXPECT_EQ(end.text, "Booked the tour for Ada.");
  ASSERT_EQ(steps_.size(), 5u);
  EXPECT_EQ(steps_[0].tool, "open");
  EXPECT_EQ(steps_[0].result.FindInt("tab"), 1);
  EXPECT_THAT(*steps_[1].result.FindString("text"),
              HasSubstr("[2] button \"Send\""));
  EXPECT_EQ(steps_[2].tool, "type");
  EXPECT_EQ(steps_[3].tool, "click");
  EXPECT_FALSE(steps_[3].result.FindString("error"));
  tabs::TabInterface* tab = browser()->tab_strip_model()->GetTabAtIndex(1);
  EXPECT_EQ(content::EvalJs(tab->GetContents(),
                            "document.getElementById('said').textContent"),
            "Ada");
  EXPECT_EQ(browser()->tab_strip_model()->GetActiveTab(), chat_);

  const std::vector<std::string> requests = model_.Requests();
  ASSERT_EQ(requests.size(), 4u);
  std::optional<base::DictValue> first =
      base::JSONReader::ReadDict(requests[0], base::JSON_PARSE_RFC);
  ASSERT_TRUE(first);
  EXPECT_EQ(*first->FindString("model"), "test-agent");
  EXPECT_EQ(first->FindList("tools")->size(), 8u);
  const base::ListValue& messages = *first->FindList("messages");
  ASSERT_EQ(messages.size(), 2u);
  EXPECT_EQ(*messages[1].GetDict().FindString("content"),
            "Book a tour for Ada");
  EXPECT_THAT(requests[2], HasSubstr("Your name"));
}

IN_PROC_BROWSER_TEST_F(AgentLoopBrowserTest,
                       AFollowUpStartsWithoutPageTextAndCanRecallIt) {
  model_.Script(
      {ScriptedModel::Calls(base::ListValue().Append(ScriptedModel::Call(
           "open", base::DictValue().Set("url", Url("/form"))))),
       ScriptedModel::Calls(base::ListValue().Append(
           ScriptedModel::Call("read", base::DictValue().Set("tab", 1)))),
       ScriptedModel::Answer("The form asks for your name.")});
  ASSERT_EQ(Ask("What does the form ask for?").kind, Kind::kAnswer);
  model_.Script(
      {ScriptedModel::Calls(base::ListValue().Append(
           ScriptedModel::Call("recall", base::DictValue().Set("step", 2)))),
       ScriptedModel::Answer("It says \"Your name\".")});

  const AgentLoop::Step& end = Ask("What exactly did it say?");

  EXPECT_EQ(end.kind, Kind::kAnswer);
  ASSERT_EQ(steps_.size(), 2u);
  EXPECT_EQ(steps_[0].tool, "recall");
  EXPECT_THAT(*steps_[0].result.FindStringByDottedPath("result.text"),
              HasSubstr("Your name"));
  const std::vector<std::string> requests = model_.Requests();
  ASSERT_EQ(requests.size(), 5u);
  EXPECT_THAT(requests[2], HasSubstr("Your name"));
  EXPECT_THAT(requests[3], Not(HasSubstr("Your name")));
  EXPECT_THAT(requests[3], HasSubstr("Call recall with step 2"));
  EXPECT_THAT(requests[4], HasSubstr("Your name"));
}

IN_PROC_BROWSER_TEST_F(AgentLoopBrowserTest, AToolThatFailsIsToldToTheModel) {
  model_.Script({ScriptedModel::Calls(
                     base::ListValue().Append(ScriptedModel::Call("fly"))),
                 ScriptedModel::Answer("I can't fly.")});

  const AgentLoop::Step& end = Ask("Fly to Lisbon");

  EXPECT_EQ(end.kind, Kind::kAnswer);
  ASSERT_EQ(steps_.size(), 2u);
  EXPECT_EQ(*steps_[0].result.FindString("error"),
            "There is no tool called fly.");
  EXPECT_THAT(model_.Requests()[1], HasSubstr("There is no tool called fly."));
}

IN_PROC_BROWSER_TEST_F(AgentLoopBrowserTest, SaysWhenTheModelFails) {
  model_.Script({"{}"}, net::HTTP_INTERNAL_SERVER_ERROR);
  EXPECT_EQ(Ask("Hello").text, "The model request failed: HTTP 500");

  model_.Script({R"({"error":{"message":"The context is too long."}})"},
                net::HTTP_BAD_REQUEST);
  EXPECT_EQ(Ask("Hello").text,
            "The model request failed: HTTP 400: The context is too long.");

  model_.Script({"not an answer"});
  EXPECT_EQ(Ask("Hello").text, "The model's answer could not be read.");
}

IN_PROC_BROWSER_TEST_F(AgentLoopBrowserTest, StopsWhenItGoesRoundInCircles) {
  model_.Script({ScriptedModel::Calls(
      base::ListValue().Append(ScriptedModel::Call("fly")))});

  const AgentLoop::Step& end = Ask("Fly to Lisbon");

  EXPECT_EQ(end.kind, Kind::kFailed);
  EXPECT_EQ(end.text, "The agent stopped after 40 steps without an answer.");
  EXPECT_EQ(model_.Requests().size(), 40u);
}

}  // namespace
}  // namespace uw
