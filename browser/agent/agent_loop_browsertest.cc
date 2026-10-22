// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/browser/agent/agent_loop.h"

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "base/environment.h"
#include "base/files/scoped_temp_dir.h"
#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/run_loop.h"
#include "base/strings/strcat.h"
#include "base/task/sequenced_task_runner.h"
#include "base/test/bind.h"
#include "base/test/run_until.h"
#include "base/test/test_timeouts.h"
#include "base/threading/thread_restrictions.h"
#include "chrome/browser/actor/actor_keyed_service.h"
#include "chrome/browser/headless/test/headless_mode_browsertest.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "components/prefs/pref_service.h"
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
#include "uw/browser/ai_providers/api_key_store.h"
#include "uw/browser/ai_providers/catalog_store.h"
#include "uw/browser/ai_providers/model_test_server.h"
#include "uw/browser/ai_providers/scoped_memory_keychain.h"
#include "uw/browser/terminal/terminal_shell.h"
#include "uw/browser/terminal/terminal_tab.h"
#include "uw/components/ai_providers/providers.h"
#include "uw/components/terminal/shell_host.h"

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
  EXPECT_EQ(first->FindList("tools")->size(), 11u);
  const base::ListValue& messages = *first->FindList("messages");
  ASSERT_EQ(messages.size(), 2u);
  EXPECT_EQ(*messages[1].GetDict().FindString("content"),
            "Book a tour for Ada");
  EXPECT_THAT(requests[2], HasSubstr("Your name"));
}

IN_PROC_BROWSER_TEST_F(AgentLoopBrowserTest,
                       AFollowUpKeepsThePageTextAndCanRecallIt) {
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
  EXPECT_THAT(requests[3], HasSubstr("Your name"));
  EXPECT_THAT(requests[3], Not(HasSubstr("Call recall with step 2")));
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

// The agent's terminal: commands in a terminal tab under the chat, in the
// user's login shell.
class AgentTerminalBrowserTest : public AgentLoopBrowserTest {
 protected:
  void SetUpOnMainThread() override {
    AgentLoopBrowserTest::SetUpOnMainThread();
    base::ScopedAllowBlockingForTesting allow_blocking;
    ASSERT_TRUE(zdotdir_.CreateUniqueTempDir());
    // zsh finds none of the user's startup files in an empty folder, so the
    // tests do not depend on them.
    base::Environment::Create()->SetVar("ZDOTDIR", zdotdir_.GetPath().value());
  }

  static base::ListValue Run(base::DictValue args) {
    return base::ListValue().Append(
        ScriptedModel::Call("run", std::move(args)));
  }

  // What the terminal showing `contents` holds, as the agent reads it.
  static std::string ScreenOf(content::WebContents* contents) {
    std::optional<std::string> screen;
    TerminalShell::From(contents)->Read(
        -100, base::BindLambdaForTesting(
                  [&](const std::string& text, int) { screen = text; }));
    EXPECT_TRUE(base::test::RunUntil([&] { return screen.has_value(); }));
    return screen.value_or(std::string());
  }

  // What the terminal showing `contents` waits for.
  static ShellHost::Activity ActivityOf(content::WebContents* contents) {
    std::optional<ShellHost::Activity> activity;
    TerminalShell::From(contents)->GetActivity(base::BindLambdaForTesting(
        [&](ShellHost::Activity now) { activity = std::move(now); }));
    EXPECT_TRUE(base::test::RunUntil([&] { return activity.has_value(); }));
    return activity.value_or(ShellHost::Activity());
  }

  // Waits until the terminal showing `contents` reads as `wanted`.
  static bool WaitFor(content::WebContents* contents,
                      bool (*wanted)(const ShellHost::Activity&)) {
    const base::TimeTicks deadline =
        base::TimeTicks::Now() + TestTimeouts::action_timeout();
    while (base::TimeTicks::Now() < deadline) {
      if (wanted(ActivityOf(contents))) {
        return true;
      }
      base::RunLoop pause;
      base::SequencedTaskRunner::GetCurrentDefault()->PostDelayedTask(
          FROM_HERE, pause.QuitClosure(), base::Milliseconds(50));
      pause.Run();
    }
    return false;
  }

  // Calls the agent's `tool` directly, as the loop would, and waits for it.
  base::DictValue CallTool(std::string_view tool, base::DictValue args) {
    std::optional<base::DictValue> result;
    agent_->tools().Call(tool, args,
                         base::BindLambdaForTesting([&](base::DictValue done) {
                           result = std::move(done);
                         }));
    EXPECT_TRUE(base::test::RunUntil([&] { return result.has_value(); }));
    return result ? std::move(*result) : base::DictValue();
  }

  base::ScopedTempDir zdotdir_;
};

IN_PROC_BROWSER_TEST_F(AgentTerminalBrowserTest,
                       RunsACommandInATerminalTabUnderTheChat) {
  model_.Script({ScriptedModel::Calls(Run(
                     base::DictValue().Set("command", "echo hi-$((40+2))"))),
                 ScriptedModel::Answer("It said hi.")});

  EXPECT_EQ(Ask("Say hi in a terminal").kind, Kind::kAnswer);

  ASSERT_EQ(steps_.size(), 2u);
  const base::DictValue& ran = steps_[0].result;
  EXPECT_FALSE(ran.contains("error")) << ran.DebugString();
  EXPECT_EQ(ran.FindInt("tab"), 1);
  EXPECT_EQ(*ran.FindString("folder"), "~");
  EXPECT_EQ(*ran.FindString("output"), "hi-42");
  EXPECT_FALSE(ran.contains("running"));
  EXPECT_THAT(model_.Requests().back(), HasSubstr("hi-42"));
  ASSERT_TRUE(agent_->tools().IsTerminalTab(1));
  content::WebContents* terminal =
      agent_->tools().OpenTabs().at(1)->GetContents();
  EXPECT_TRUE(IsAgentTerminal(terminal));
  // It opened in the background, under the chat.
  EXPECT_EQ(browser()->tab_strip_model()->GetActiveTab(), chat_.get());
  EXPECT_EQ(browser()->tab_strip_model()->GetOpenerOfTabAt(
                browser()->tab_strip_model()->GetIndexOfWebContents(terminal)),
            chat_.get());
}

// A command still running when the wait ends goes on in its tab. The agent
// answers it with keys, then reads the tab.
IN_PROC_BROWSER_TEST_F(AgentTerminalBrowserTest,
                       ACommandLeftRunningTakesKeysAndIsReadLater) {
  model_.Script(
      {ScriptedModel::Calls(
           Run(base::DictValue()
                   .Set("command", "read -r name && echo hi-$name-$((40+2))")
                   .Set("wait", 0))),
       ScriptedModel::Calls(base::ListValue().Append(ScriptedModel::Call(
           "keys", base::DictValue()
                       .Set("tab", 1)
                       .Set("text", "ada")
                       .Set("keys", base::ListValue().Append("Enter"))))),
       ScriptedModel::Calls(base::ListValue().Append(
           ScriptedModel::Call("screen", base::DictValue().Set("tab", 1)))),
       ScriptedModel::Answer("It greeted Ada.")});

  EXPECT_EQ(Ask("Greet Ada in a terminal").kind, Kind::kAnswer);

  ASSERT_EQ(steps_.size(), 4u);
  const base::DictValue& left = steps_[0].result;
  EXPECT_FALSE(left.contains("error")) << left.DebugString();
  EXPECT_EQ(left.FindBool("running"), true);
  EXPECT_THAT(*left.FindString("note"), HasSubstr("keeps running in tab 1"));
  const base::DictValue& keyed = steps_[1].result;
  ASSERT_FALSE(keyed.contains("error")) << keyed.DebugString();
  EXPECT_THAT(*keyed.FindString("text"), HasSubstr("hi-ada-42"));
  const base::DictValue& read = steps_[2].result;
  ASSERT_FALSE(read.contains("error")) << read.DebugString();
  EXPECT_THAT(*read.FindString("text"), HasSubstr("hi-ada-42"));
  EXPECT_FALSE(read.contains("program"));
  EXPECT_FALSE(read.contains("password"));
}

// A password prompt goes to the user, who types it in the tab. The agent
// never sees it and never types it.
IN_PROC_BROWSER_TEST_F(AgentTerminalBrowserTest,
                       APasswordPromptWaitsForTheUser) {
  std::vector<std::pair<int, std::string>> asked;
  agent_->tools().SetOnPassword(
      base::BindLambdaForTesting([&](int number, const std::string& program) {
        asked.emplace_back(number, program);
        // The user types the first password; the second waits.
        if (number && asked.size() == 1) {
          TerminalShell::From(
              agent_->tools().OpenTabs().at(number)->GetContents())
              ->Write("hunter2\r");
        }
      }));
  model_.Script(
      {ScriptedModel::Calls(Run(base::DictValue().Set(
           "command", "read -rs secret && echo got-${#secret}-letters"))),
       ScriptedModel::Calls(base::ListValue().Append(ScriptedModel::Call(
           "run", base::DictValue().Set(
                      "command",
                      "bash -c 'stty -echo; printf Password:; read p; "
                      "stty echo'")))),
       ScriptedModel::Answer("Done.")});

  // The second command waits for a password nobody types, and the user stops
  // the chat.
  steps_.clear();
  agent_->Send("Read a secret");
  ASSERT_TRUE(base::test::RunUntil([&] { return asked.size() == 3u; }))
      << asked.size() << " password prompts";
  EXPECT_EQ(asked[0], std::make_pair(1, std::string("read")));
  EXPECT_EQ(asked[1], std::make_pair(0, std::string()));
  EXPECT_EQ(asked[2], std::make_pair(1, std::string("bash")));
  ASSERT_EQ(steps_.size(), 1u);
  EXPECT_EQ(*steps_[0].result.FindString("output"), "got-7-letters");
  agent_->Stop();
  ASSERT_TRUE(base::test::RunUntil([&] { return !agent_->working(); }));
  EXPECT_EQ(asked.back(), std::make_pair(0, std::string()));
  EXPECT_EQ(steps_.back().kind, Kind::kStoppedByUser);
  for (const std::string& request : model_.Requests()) {
    EXPECT_THAT(request, Not(HasSubstr("hunter2")));
  }
}

// A program that asks for a password after the agent went on takes no keys
// from the agent but Ctrl-C alone, which cancels it.
IN_PROC_BROWSER_TEST_F(AgentTerminalBrowserTest,
                       OnlyCtrlCReachesAPasswordPrompt) {
  const base::DictValue left =
      CallTool("run", base::DictValue()
                          .Set("command", "sleep 0.5 && read -rs secret")
                          .Set("wait", 0));
  ASSERT_EQ(left.FindBool("running"), true) << left.DebugString();
  content::WebContents* terminal =
      agent_->tools().OpenTabs().at(1)->GetContents();
  ASSERT_TRUE(WaitFor(terminal, [](const ShellHost::Activity& activity) {
    return activity.password;
  }));

  const base::DictValue keyed =
      CallTool("keys", base::DictValue()
                           .Set("tab", 1)
                           .Set("text", "guess")
                           .Set("keys", base::ListValue().Append("Enter")));

  EXPECT_THAT(*keyed.FindString("error"), HasSubstr("waits for a password"));
  EXPECT_TRUE(ActivityOf(terminal).password);

  const base::DictValue cancelled =
      CallTool("keys", base::DictValue().Set("tab", 1).Set(
                           "keys", base::ListValue().Append("Ctrl-C")));
  EXPECT_FALSE(cancelled.FindString("error")) << cancelled.DebugString();
  EXPECT_TRUE(WaitFor(terminal, [](const ShellHost::Activity& activity) {
    return activity.at_prompt && !activity.password;
  }));
}

// A program that reads keys one at a time, as ssh -t does for a whole
// session, hides a password prompt on the other machine. Its text shows it,
// so the agent types nothing there but Ctrl-C.
IN_PROC_BROWSER_TEST_F(AgentTerminalBrowserTest,
                       APasswordPromptIsReadFromItsText) {
  const base::DictValue left = CallTool(
      "run", base::DictValue()
                 .Set("command",
                      "sleep 0.5 && sh -c 'stty raw -echo; printf \"[sudo] "
                      "password for admin: \"; head -c 1 >/dev/null; stty "
                      "sane'")
                 .Set("wait", 0));
  ASSERT_EQ(left.FindBool("running"), true) << left.DebugString();
  content::WebContents* terminal =
      agent_->tools().OpenTabs().at(1)->GetContents();
  ASSERT_TRUE(WaitFor(terminal, [](const ShellHost::Activity& activity) {
    return activity.password && activity.program_reads_keys;
  }));

  const base::DictValue keyed =
      CallTool("keys", base::DictValue()
                           .Set("tab", 1)
                           .Set("text", "guess")
                           .Set("keys", base::ListValue().Append("Enter")));
  EXPECT_THAT(*keyed.FindString("error"), HasSubstr("waits for a password"));
  const base::DictValue screen =
      CallTool("screen", base::DictValue().Set("tab", 1));
  EXPECT_EQ(screen.FindBool("password"), true) << screen.DebugString();

  const base::DictValue cancelled =
      CallTool("keys", base::DictValue().Set("tab", 1).Set(
                           "keys", base::ListValue().Append("Ctrl-C")));
  EXPECT_FALSE(cancelled.FindString("error")) << cancelled.DebugString();
  EXPECT_TRUE(WaitFor(terminal, [](const ShellHost::Activity& activity) {
    return activity.at_prompt && !activity.password;
  }));
  EXPECT_THAT(ScreenOf(terminal), Not(HasSubstr("guess")));
}

// Stop ends the wait at once; the command keeps running in its tab.
IN_PROC_BROWSER_TEST_F(AgentTerminalBrowserTest,
                       StopEndsTheWaitAndTheCommandGoesOn) {
  model_.Script({ScriptedModel::Calls(
      Run(base::DictValue().Set("command", "sleep 300")))});
  steps_.clear();
  agent_->Send("Wait five minutes");
  ASSERT_TRUE(base::test::RunUntil([&] {
    const std::map<int, tabs::TabInterface*> open = agent_->tools().OpenTabs();
    return open.contains(1) && TerminalShell::From(open.at(1)->GetContents());
  }));
  content::WebContents* terminal =
      agent_->tools().OpenTabs().at(1)->GetContents();
  ASSERT_TRUE(WaitFor(terminal, [](const ShellHost::Activity& activity) {
    return activity.program == "sleep";
  }));

  agent_->Stop();
  ASSERT_TRUE(base::test::RunUntil([&] { return !agent_->working(); }));

  ASSERT_EQ(steps_.size(), 2u);
  EXPECT_EQ(steps_[0].result.FindBool("running"), true);
  EXPECT_THAT(*steps_[0].result.FindString("note"),
              HasSubstr("stopped the chat"));
  EXPECT_EQ(steps_[1].kind, Kind::kStoppedByUser);
  EXPECT_EQ(ActivityOf(terminal).program, "sleep");
}

// The agent never reaches a terminal tab the user opened, nor one of its web
// tabs as a terminal.
IN_PROC_BROWSER_TEST_F(AgentTerminalBrowserTest,
                       TheUsersOwnTerminalIsOutOfReach) {
  OpenTerminalTab(browser());
  content::WebContents* mine =
      browser()->tab_strip_model()->GetActiveWebContents();
  ASSERT_TRUE(
      base::test::RunUntil([&] { return !TerminalFolder(mine).empty(); }));
  const auto call = [](std::string_view tool, base::DictValue args) {
    return ScriptedModel::Calls(
        base::ListValue().Append(ScriptedModel::Call(tool, std::move(args))));
  };
  model_.Script(
      {call("screen", base::DictValue().Set("tab", 1)),
       call("keys", base::DictValue().Set("tab", 1).Set("text", "agent-")),
       call("run", base::DictValue().Set("tab", 1).Set("command",
                                                       "echo agent-was-here")),
       call("open", base::DictValue().Set("url", Url("/form"))),
       call("run", base::DictValue().Set("tab", 1).Set("command",
                                                       "echo agent-was-here")),
       call("run", base::DictValue().Set("command", "echo agent-$((40+2))")),
       ScriptedModel::Answer("Done.")});

  EXPECT_EQ(Ask("Use my terminal").kind, Kind::kAnswer);

  ASSERT_EQ(steps_.size(), 7u);
  for (size_t step = 0; step < 3; ++step) {
    EXPECT_THAT(*steps_[step].result.FindString("error"),
                HasSubstr("not one of the agent's tabs"));
  }
  EXPECT_THAT(*steps_[4].result.FindString("error"),
              HasSubstr("Tab 1 is not one of your terminal tabs"));
  const base::DictValue& ran = steps_[5].result;
  ASSERT_FALSE(ran.contains("error")) << ran.DebugString();
  EXPECT_EQ(ran.FindInt("tab"), 2);
  EXPECT_EQ(*ran.FindString("output"), "agent-42");
  EXPECT_NE(agent_->tools().OpenTabs().at(2)->GetContents(), mine);
  EXPECT_FALSE(IsAgentTerminal(mine));
  EXPECT_THAT(ScreenOf(mine), Not(HasSubstr("agent-")));
}

// A decision model at the test server is the command check. An approver
// answers each step that asks, and keeps what it asked.
class AgentCheckBrowserTest : public AgentTerminalBrowserTest {
 protected:
  void SetUpOnMainThread() override {
    AgentTerminalBrowserTest::SetUpOnMainThread();
    PrefService& prefs = *browser()->GetProfile()->GetPrefs();
    Provider provider = PresetProvider(ProviderPreset::kCustom);
    provider.name = "Check";
    provider.address = embedded_test_server()->GetURL("/v1").spec();
    provider.format = ProviderFormat::kOpenAiDecisions;
    provider.id = AddProvider(prefs, provider);
    provider.key_account = KeyAccountFor(provider.id);
    ASSERT_TRUE(ApiKeyStore().SetKey(provider.key_account, "check-key"));
    UpdateProvider(prefs, provider);
    SetChoice(prefs, ModelSlot::kDecision, provider.id, "check-model");
    agent_->SetApprover(base::BindLambdaForTesting(
        [this](AgentTools::Approval approval,
               base::OnceCallback<void(AgentLoop::ApprovalAnswer)> answer) {
          asked_.push_back(std::move(approval));
          std::move(answer).Run(answer_);
        }));
  }

  // The body of each command check so far.
  std::vector<std::string> Checks() {
    std::vector<std::string> bodies;
    for (const ModelRequest& request : checks_.Requests()) {
      if (request.path == "/v1/decisions") {
        bodies.push_back(request.body);
      }
    }
    return bodies;
  }

  ScopedMemoryKeychain keychain_;
  ModelTestServer checks_{embedded_test_server()};
  std::vector<AgentTools::Approval> asked_;
  AgentLoop::ApprovalAnswer answer_;
};

// The check sees what the user asked and what the agent did, never a page's
// text or a command's output. Safe steps run without asking.
IN_PROC_BROWSER_TEST_F(AgentCheckBrowserTest, TheCheckSeesNoPageOrOutput) {
  checks_.Answer("/v1/decisions", CheckDecisionsAnswer(0.95, "safe", 0.99));
  model_.Script(
      {ScriptedModel::Calls(base::ListValue().Append(ScriptedModel::Call(
           "open", base::DictValue().Set("url", Url("/form"))))),
       ScriptedModel::Calls(base::ListValue().Append(
           ScriptedModel::Call("read", base::DictValue().Set("tab", 1)))),
       ScriptedModel::Calls(
           Run(base::DictValue().Set("command", "echo out-$((6*7))"))),
       ScriptedModel::Calls(Run(base::DictValue().Set("command", "pwd"))),
       ScriptedModel::Answer("Done.")});

  EXPECT_EQ(Ask("Note the tour page in a terminal").kind, Kind::kAnswer);

  EXPECT_TRUE(asked_.empty());
  ASSERT_EQ(steps_.size(), 5u);
  EXPECT_EQ(*steps_[2].result.FindString("output"), "out-42");
  EXPECT_THAT(model_.Requests().back(), HasSubstr("Your name"));
  const std::vector<std::string> checks = Checks();
  ASSERT_EQ(checks.size(), 2u);
  EXPECT_THAT(checks[1], HasSubstr("Note the tour page in a terminal"));
  EXPECT_THAT(checks[1], HasSubstr("echo out-$((6*7))"));
  EXPECT_THAT(checks[1], HasSubstr(R"(\npwd")"));
  for (const std::string& check : checks) {
    EXPECT_THAT(check, Not(HasSubstr("Your name")));
    EXPECT_THAT(check, Not(HasSubstr("Book a tour")));
    EXPECT_THAT(check, Not(HasSubstr("out-42")));
  }
}

// No answer, or an unsure one, asks. A declined command does not run.
IN_PROC_BROWSER_TEST_F(AgentCheckBrowserTest, AnUnsureOrMissingAnswerAsks) {
  checks_.Fail("/v1/decisions", net::HTTP_INTERNAL_SERVER_ERROR, "{}");
  model_.Script({ScriptedModel::Calls(
                     Run(base::DictValue().Set("command", "echo first"))),
                 ScriptedModel::Answer("I left it.")});
  EXPECT_EQ(Ask("Say first").kind, Kind::kAnswer);

  checks_.Answer("/v1/decisions", CheckDecisionsAnswer(0.6, "safe", 0.99));
  model_.Script({ScriptedModel::Calls(
                     Run(base::DictValue().Set("command", "echo second"))),
                 ScriptedModel::Answer("I left it.")});
  EXPECT_EQ(Ask("Say second").kind, Kind::kAnswer);

  ASSERT_EQ(asked_.size(), 2u);
  EXPECT_EQ(asked_[0].command, "echo first");
  EXPECT_EQ(asked_[0].reason, "The command check didn't answer.");
  EXPECT_EQ(asked_[1].command, "echo second");
  EXPECT_EQ(asked_[1].reason, "The command check wasn't sure it's safe.");
  EXPECT_FALSE(asked_[1].sensitive);
  EXPECT_THAT(*steps_[0].result.FindString("error"),
              HasSubstr("did not approve"));
  EXPECT_TRUE(agent_->tools().OpenTabs().empty());
}

// Auto asks before a dangerous command, with the danger the check found, and
// runs it once the user approves.
IN_PROC_BROWSER_TEST_F(AgentCheckBrowserTest, ADangerousCommandAsksFirst) {
  checks_.Answer("/v1/decisions", CheckDecisionsAnswer(0.05, "deletes", 0.99));
  answer_.choice = AgentLoop::ApprovalAnswer::Choice::kApprove;
  model_.Script({ScriptedModel::Calls(Run(
                     base::DictValue().Set("command", "echo gone-$((40+2))"))),
                 ScriptedModel::Answer("Gone.")});

  EXPECT_EQ(Ask("Clear the old builds").kind, Kind::kAnswer);

  ASSERT_EQ(asked_.size(), 1u);
  EXPECT_EQ(asked_[0].tab, 1);
  EXPECT_EQ(asked_[0].folder, "~");
  EXPECT_EQ(asked_[0].command, "echo gone-$((40+2))");
  EXPECT_EQ(asked_[0].reason,
            "Deletes or overwrites files or data. What it removes may not come "
            "back.");
  EXPECT_EQ(*steps_[0].result.FindString("output"), "gone-42");
}

// Full auto asks the check only about sensitive files: a dangerous command
// runs, and one that may read a .env file asks, naming it.
IN_PROC_BROWSER_TEST_F(AgentCheckBrowserTest,
                       FullAutoAsksOnlyBeforeASensitiveFile) {
  agent_->SetMode(ApprovalMode::kFullAuto);
  checks_.Answer("/v1/decisions", CheckDecisionsAnswer(0.05, "deletes", 0.99));
  checks_.AnswerAbout("/v1/decisions", "cat .env",
                      CheckDecisionsAnswer(1.0, "safe", 0.1, "w2"));
  answer_.note = "Not my secrets";
  model_.Script(
      {ScriptedModel::Calls(
           Run(base::DictValue().Set("command", "echo cleaned"))),
       ScriptedModel::Calls(Run(base::DictValue().Set("command", "cat .env"))),
       ScriptedModel::Answer("I left it.")});

  EXPECT_EQ(Ask("Clean up, then show the env file").kind, Kind::kAnswer);

  ASSERT_EQ(asked_.size(), 1u);
  EXPECT_TRUE(asked_[0].sensitive);
  EXPECT_EQ(asked_[0].file, ".env");
  EXPECT_EQ(asked_[0].command, "cat .env");
  EXPECT_EQ(asked_[0].reason,
            "A sensitive file. It can hold passwords and keys, and what the "
            "command prints goes to test-agent.");
  EXPECT_EQ(*steps_[0].result.FindString("output"), "cleaned");
  EXPECT_EQ(*steps_[1].result.FindString("user_said"), "Not my secrets");
  const std::vector<std::string> checks = Checks();
  ASSERT_EQ(checks.size(), 2u);
  for (const std::string& check : checks) {
    EXPECT_THAT(check, Not(HasSubstr(R"("name":"danger")")));
    EXPECT_THAT(check, HasSubstr(R"("name":"file")"));
  }
}

// Ask first asks before every command, without the check.
IN_PROC_BROWSER_TEST_F(AgentCheckBrowserTest, AskFirstAsksWithoutTheCheck) {
  agent_->SetMode(ApprovalMode::kAskFirst);
  model_.Script(
      {ScriptedModel::Calls(Run(base::DictValue().Set("command", "echo hi"))),
       ScriptedModel::Answer("I left it.")});

  EXPECT_EQ(Ask("Say hi").kind, Kind::kAnswer);

  ASSERT_EQ(asked_.size(), 1u);
  EXPECT_EQ(asked_[0].reason, "Ask first is on.");
  EXPECT_TRUE(Checks().empty());
}

// Stop drops a check under way, and its command never runs.
IN_PROC_BROWSER_TEST_F(AgentCheckBrowserTest, StopDropsACheckUnderWay) {
  checks_.Hold("/v1/decisions");
  model_.Script({ScriptedModel::Calls(
      Run(base::DictValue().Set("command", "echo never")))});
  steps_.clear();
  agent_->Send("Say never");
  ASSERT_TRUE(base::test::RunUntil([&] { return !Checks().empty(); }));

  agent_->Stop();
  ASSERT_TRUE(base::test::RunUntil([&] { return !agent_->working(); }));

  ASSERT_EQ(steps_.size(), 1u);
  EXPECT_EQ(steps_[0].kind, Kind::kStoppedByUser);
  EXPECT_TRUE(asked_.empty());
  EXPECT_TRUE(agent_->tools().OpenTabs().empty());
}

// Without a decision or language model, the chat's own model checks.
// The language model checks commands, with thinking off where the catalog
// says how. It answers under /v1/check, apart from the agent's model.
class AgentLanguageCheckBrowserTest : public AgentTerminalBrowserTest {
 protected:
  static constexpr char kCheckPath[] = "/v1/check/chat/completions";

  void SetUpOnMainThread() override {
    AgentTerminalBrowserTest::SetUpOnMainThread();
    PrefService& prefs = *browser()->GetProfile()->GetPrefs();
    Provider provider = PresetProvider(ProviderPreset::kCustom);
    provider.name = "Check";
    provider.address = embedded_test_server()->GetURL("/v1/check").spec();
    provider.format = ProviderFormat::kChatCompletions;
    provider.id = AddProvider(prefs, provider);
    provider.key_account = KeyAccountFor(provider.id);
    ASSERT_TRUE(ApiKeyStore().SetKey(provider.key_account, "check-key"));
    UpdateProvider(prefs, provider);
    SetChoice(prefs, ModelSlot::kLanguage, provider.id, "check-model");
    // The catalog says the model can switch its thinking off.
    checks_.Answer(
        "/v1/catalog.json",
        base::StrCat({R"({"checker": {"api": ")", provider.address,
                      R"(", "models": {"check-model": {"reasoning_options":)"
                      R"( [{"type": "toggle"}]}}}})"}));
    CatalogStore::SetSourceForTesting(
        embedded_test_server()->GetURL("/v1/catalog.json"));
    checks_.Answer(kCheckPath,
                   ChatAnswer(R"({\"danger\": \"safe\", \"confidence\": )"
                              R"(0.95, \"reason\": \"\", \"file\": )"
                              R"(\"none\", \"file_confidence\": 0.99})"));
    agent_->SetApprover(base::BindLambdaForTesting(
        [this](AgentTools::Approval approval,
               base::OnceCallback<void(AgentLoop::ApprovalAnswer)> answer) {
          asked_.push_back(std::move(approval));
          std::move(answer).Run(AgentLoop::ApprovalAnswer());
        }));
    model_.Script(
        {ScriptedModel::Calls(Run(base::DictValue().Set("command", "echo hi"))),
         ScriptedModel::Answer("It said hi.")});
  }

  // The body of each command check so far.
  std::vector<std::string> Checks() {
    std::vector<std::string> bodies;
    for (const ModelRequest& request : checks_.Requests()) {
      if (request.path == kCheckPath) {
        bodies.push_back(request.body);
      }
    }
    return bodies;
  }

  ScopedMemoryKeychain keychain_;
  ModelTestServer checks_{embedded_test_server()};
  std::vector<AgentTools::Approval> asked_;
};

IN_PROC_BROWSER_TEST_F(AgentLanguageCheckBrowserTest,
                       TheCheckAsksWithThinkingOff) {
  EXPECT_EQ(Ask("Say hi in a terminal").kind, Kind::kAnswer);

  EXPECT_TRUE(asked_.empty());
  EXPECT_EQ(*steps_[0].result.FindString("output"), "hi");
  const std::vector<std::string> checks = Checks();
  ASSERT_EQ(checks.size(), 1u);
  EXPECT_THAT(checks[0], HasSubstr(R"("thinking":{"type":"disabled"})"));
}

// A provider that rejects the switch is asked again without it, and the
// command runs without asking.
IN_PROC_BROWSER_TEST_F(AgentLanguageCheckBrowserTest,
                       AProviderThatRejectsTheSwitchIsAskedAgain) {
  checks_.FailAbout(kCheckPath, R"("thinking":{)", net::HTTP_BAD_REQUEST,
                    R"({"error": {"message": "Unknown field: thinking"}})");

  EXPECT_EQ(Ask("Say hi in a terminal").kind, Kind::kAnswer);

  EXPECT_TRUE(asked_.empty());
  EXPECT_EQ(*steps_[0].result.FindString("output"), "hi");
  const std::vector<std::string> checks = Checks();
  ASSERT_EQ(checks.size(), 2u);
  EXPECT_THAT(checks[0], HasSubstr(R"("thinking":{)"));
  EXPECT_THAT(checks[1], Not(HasSubstr(R"("thinking":{)")));
}

IN_PROC_BROWSER_TEST_F(AgentTerminalBrowserTest,
                       WithoutADecisionModelTheChatsModelChecks) {
  std::vector<AgentTools::Approval> asked;
  agent_->SetApprover(base::BindLambdaForTesting(
      [&](AgentTools::Approval approval,
          base::OnceCallback<void(AgentLoop::ApprovalAnswer)> answer) {
        asked.push_back(approval);
        std::move(answer).Run(AgentLoop::ApprovalAnswer());
      }));
  model_.Script(
      {ScriptedModel::Calls(Run(base::DictValue().Set("command", "echo hi"))),
       ScriptedModel::Answer(
           R"({"danger": "safe", "confidence": 0.95, "reason": "", )"
           R"("file": "none", "file_confidence": 0.99})"),
       ScriptedModel::Answer("It said hi.")});

  EXPECT_EQ(Ask("Say hi in a terminal").kind, Kind::kAnswer);

  EXPECT_TRUE(asked.empty());
  EXPECT_EQ(*steps_[0].result.FindString("output"), "hi");
  ASSERT_EQ(model_.Requests().size(), 3u);
  EXPECT_THAT(model_.Requests()[1], HasSubstr("You check a step"));
}

}  // namespace
}  // namespace uw
