// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/browser/agent/agent_loop.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/test/bind.h"
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
#include "uw/components/ai_providers/providers.h"

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
    UseFormat(ProviderFormat::kChatCompletions);
  }

  void TearDownOnMainThread() override {
    agent_.reset();
    actor::ActorKeyedService::Get(browser()->GetProfile())->ResetForTesting();
    HeadlessModeBrowserTest::TearDownOnMainThread();
  }

  // Talks to the model in `format` from now on.
  void UseFormat(ProviderFormat format) {
    AgentModel model;
    model.connection.endpoint = embedded_test_server()->GetURL("/v1").spec();
    model.connection.format = format;
    model.connection.model = "test-agent";
    model.api_key = "agent-key";
    agent_ = std::make_unique<AgentLoop>(
        *chat_, std::move(model),
        base::BindRepeating(&AgentLoopBrowserTest::OnStep,
                            base::Unretained(this)));
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

// Over OpenAI Responses, a tool call and its result travel as Responses items,
// and the sealed reasoning that came with the call goes back with them.
IN_PROC_BROWSER_TEST_F(AgentLoopBrowserTest,
                       AChatOverResponsesSendsItsReasoningBack) {
  UseFormat(ProviderFormat::kOpenAiResponses);
  model_.Script(
      {R"({"status":"completed","output":[)"
       R"({"type":"reasoning","id":"rs_1","encrypted_content":"sealed"},)"
       R"({"type":"function_call","call_id":"call_1","name":"fly",)"
       R"("arguments":"{}"}]})",
       R"({"status":"completed","output":[{"type":"message",)"
       R"("role":"assistant","content":[{"type":"output_text",)"
       R"("text":"I can't fly."}]}]})"});

  const AgentLoop::Step& end = Ask("Fly to Lisbon");

  EXPECT_EQ(end.kind, Kind::kAnswer);
  EXPECT_EQ(end.text, "I can't fly.");
  const std::vector<std::string> requests = model_.Requests();
  ASSERT_EQ(requests.size(), 2u);
  EXPECT_THAT(requests[0], HasSubstr(R"("store":false)"));
  EXPECT_THAT(requests[0], HasSubstr(R"("instructions":)"));
  EXPECT_THAT(requests[1], HasSubstr(R"("encrypted_content":"sealed")"));
  EXPECT_THAT(requests[1], HasSubstr(R"("type":"function_call_output")"));
  EXPECT_THAT(requests[1], Not(HasSubstr("uw_kept")));
}

// Over Anthropic Messages, the tool call is a tool_use block, its result a
// tool_result block, and the signed thinking goes back before them.
IN_PROC_BROWSER_TEST_F(AgentLoopBrowserTest,
                       AChatOverAnthropicMessagesSendsItsThinkingBack) {
  UseFormat(ProviderFormat::kAnthropicMessages);
  model_.Script(
      {R"({"stop_reason":"tool_use","content":[)"
       R"({"type":"thinking","thinking":"No wings.","signature":"sig"},)"
       R"({"type":"tool_use","id":"toolu_1","name":"fly","input":{}}]})",
       R"({"stop_reason":"end_turn","content":[)"
       R"({"type":"text","text":"I can't fly."}]})"});

  const AgentLoop::Step& end = Ask("Fly to Lisbon");

  EXPECT_EQ(end.kind, Kind::kAnswer);
  EXPECT_EQ(end.text, "I can't fly.");
  const std::vector<std::string> requests = model_.Requests();
  ASSERT_EQ(requests.size(), 2u);
  EXPECT_THAT(requests[0], HasSubstr(R"("system":)"));
  EXPECT_THAT(requests[0], HasSubstr(R"("max_tokens":16000)"));
  EXPECT_THAT(requests[1], HasSubstr(R"("signature":"sig")"));
  EXPECT_THAT(requests[1], HasSubstr(R"("type":"tool_result")"));
  EXPECT_THAT(requests[1], HasSubstr(R"("tool_use_id":"toolu_1")"));
}

// Steps in one answer run in order, and the first that fails ends the rest:
// they do not run, and the model hears why.
IN_PROC_BROWSER_TEST_F(AgentLoopBrowserTest,
                       AFailedStepEndsTheRestOfItsAnswer) {
  model_.Script(
      {ScriptedModel::Calls(base::ListValue().Append(ScriptedModel::Call(
           "open", base::DictValue().Set("url", Url("/form"))))),
       ScriptedModel::Calls(base::ListValue().Append(
           ScriptedModel::Call("read", base::DictValue().Set("tab", 1)))),
       ScriptedModel::Calls(
           base::ListValue()
               .Append(ScriptedModel::Call(
                   "click", base::DictValue().Set("tab", 1).Set("element", 9)))
               .Append(ScriptedModel::Call("type", base::DictValue()
                                                       .Set("tab", 1)
                                                       .Set("element", 1)
                                                       .Set("text", "Ada")))
               .Append(ScriptedModel::Call(
                   "click",
                   base::DictValue().Set("tab", 1).Set("element", 2)))),
       ScriptedModel::Answer("The form has no such button.")});

  const AgentLoop::Step& end = Ask("Book a tour for Ada");

  EXPECT_EQ(end.kind, Kind::kAnswer);
  ASSERT_EQ(steps_.size(), 4u);
  EXPECT_EQ(steps_[2].tool, "click");
  EXPECT_TRUE(steps_[2].result.FindString("error"));
  tabs::TabInterface* tab = browser()->tab_strip_model()->GetTabAtIndex(1);
  EXPECT_EQ(content::EvalJs(tab->GetContents(),
                            "document.getElementById('name').value"),
            "");
  const std::vector<std::string> requests = model_.Requests();
  ASSERT_EQ(requests.size(), 4u);
  EXPECT_THAT(requests[3], HasSubstr("Not run, because step 3 did not work."));
}

// A change the user declines ends the rest of its answer too.
IN_PROC_BROWSER_TEST_F(AgentLoopBrowserTest,
                       ADeclinedChangeEndsTheRestOfItsAnswer) {
  std::vector<std::string> asked;
  agent_->SetApprover(base::BindLambdaForTesting(
      [&](AgentTools::Approval approval,
          base::OnceCallback<void(AgentLoop::ApprovalAnswer)> done) {
        asked.push_back(approval.action);
        std::move(done).Run({AgentLoop::ApprovalAnswer::Choice::kDecline});
      }));
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
               .Append(ScriptedModel::Call("click", base::DictValue()
                                                        .Set("tab", 1)
                                                        .Set("element", 2)
                                                        .Set("changes", true)))
               .Append(ScriptedModel::Call("type", base::DictValue()
                                                       .Set("tab", 1)
                                                       .Set("element", 1)
                                                       .Set("text", "Bob")))),
       ScriptedModel::Answer("You declined, so nothing was sent.")});

  const AgentLoop::Step& end = Ask("Book a tour for Ada");

  EXPECT_EQ(end.kind, Kind::kAnswer);
  EXPECT_EQ(asked.size(), 1u);
  ASSERT_EQ(steps_.size(), 5u);
  EXPECT_EQ(steps_[2].tool, "type");
  EXPECT_FALSE(steps_[2].result.FindString("error"));
  EXPECT_EQ(steps_[3].tool, "click");
  EXPECT_TRUE(steps_[3].result.FindString("error"));
  tabs::TabInterface* tab = browser()->tab_strip_model()->GetTabAtIndex(1);
  EXPECT_EQ(content::EvalJs(tab->GetContents(),
                            "document.getElementById('name').value + '|' + "
                            "document.getElementById('said').textContent"),
            "Ada|");
  EXPECT_THAT(model_.Requests()[3],
              HasSubstr("Not run, because step 4 did not work."));
}

// Ask first asks before every click and typing, and Full auto before none,
// not even a step the model marks as a change.
IN_PROC_BROWSER_TEST_F(AgentLoopBrowserTest, EachModeAsksAsItSays) {
  std::vector<std::string> reasons;
  agent_->SetApprover(base::BindLambdaForTesting(
      [&](AgentTools::Approval approval,
          base::OnceCallback<void(AgentLoop::ApprovalAnswer)> done) {
        reasons.push_back(approval.reason);
        std::move(done).Run({AgentLoop::ApprovalAnswer::Choice::kApprove});
      }));
  // Types `name` and clicks Send, after opening the form on the first run.
  const auto script = [&](std::string_view name, bool open) {
    std::vector<std::string> replies;
    if (open) {
      replies.push_back(
          ScriptedModel::Calls(base::ListValue().Append(ScriptedModel::Call(
              "open", base::DictValue().Set("url", Url("/form"))))));
    }
    replies.push_back(ScriptedModel::Calls(base::ListValue().Append(
        ScriptedModel::Call("read", base::DictValue().Set("tab", 1)))));
    replies.push_back(ScriptedModel::Calls(
        base::ListValue()
            .Append(ScriptedModel::Call("type", base::DictValue()
                                                    .Set("tab", 1)
                                                    .Set("element", 1)
                                                    .Set("text", name)))
            .Append(ScriptedModel::Call("click", base::DictValue()
                                                     .Set("tab", 1)
                                                     .Set("element", 2)
                                                     .Set("changes", true)))));
    replies.push_back(ScriptedModel::Answer("Sent."));
    model_.Script(std::move(replies));
  };

  agent_->SetMode(ApprovalMode::kAskFirst);
  script("Ada", /*open=*/true);
  ASSERT_EQ(Ask("Send Ada").kind, Kind::kAnswer);
  EXPECT_EQ(reasons,
            (std::vector<std::string>{
                "Ask first is on.", "The agent says this changes something."}));

  reasons.clear();
  agent_->SetMode(ApprovalMode::kAuto);
  script("Bob", /*open=*/false);
  ASSERT_EQ(Ask("Send Bob").kind, Kind::kAnswer);
  EXPECT_EQ(
      reasons,
      (std::vector<std::string>{"The agent says this changes something."}));

  reasons.clear();
  agent_->SetMode(ApprovalMode::kFullAuto);
  script("Cy", /*open=*/false);
  ASSERT_EQ(Ask("Send Cy").kind, Kind::kAnswer);
  EXPECT_TRUE(reasons.empty());
  tabs::TabInterface* tab = browser()->tab_strip_model()->GetTabAtIndex(1);
  EXPECT_EQ(content::EvalJs(tab->GetContents(),
                            "document.getElementById('said').textContent"),
            "Cy");
}

// A steer reaches a request that waits for the model: it is sent again with
// the user's new message. A stop ends the run at once.
IN_PROC_BROWSER_TEST_F(AgentLoopBrowserTest,
                       AStopOrASteerReachesAWaitingRequest) {
  model_.Script({ScriptedModel::Hang()});
  steps_.clear();
  agent_->Send("Find a tour");
  ASSERT_TRUE(
      base::test::RunUntil([&] { return model_.Requests().size() == 1u; }));
  model_.Script({ScriptedModel::Answer("Sunday at 10.")});
  agent_->Steer("Make it Sunday");
  ASSERT_TRUE(base::test::RunUntil([&] { return !agent_->working(); }));
  EXPECT_EQ(steps_.back().text, "Sunday at 10.");
  std::vector<std::string> requests = model_.Requests();
  ASSERT_EQ(requests.size(), 2u);
  EXPECT_THAT(requests[1], HasSubstr("Find a tour"));
  EXPECT_THAT(requests[1], HasSubstr("Make it Sunday"));

  model_.Script({ScriptedModel::Hang()});
  agent_->Send("And Monday?");
  ASSERT_TRUE(
      base::test::RunUntil([&] { return model_.Requests().size() == 3u; }));
  agent_->Stop();
  EXPECT_FALSE(agent_->working());
  EXPECT_EQ(steps_.back().kind, Kind::kStoppedByUser);
}

}  // namespace
}  // namespace uw
