// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/browser/agent/agent_loop.h"

#include <deque>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/strings/string_number_conversions.h"
#include "base/synchronization/lock.h"
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

namespace uw {
namespace {

using ::testing::HasSubstr;
using Kind = AgentLoop::Step::Kind;

constexpr char kForm[] = R"(<!doctype html>
<title>Book a tour</title>
<label for="name">Your name</label>
<input id="name" name="name">
<button onclick="document.getElementById('said').textContent =
    document.getElementById('name').value">Send</button>
<p id="said"></p>)";

// One tool call for Calls().
base::DictValue Call(std::string_view name,
                     base::DictValue args = base::DictValue()) {
  return base::DictValue().Set("name", name).Set("args", std::move(args));
}

// A chat-completions answer that makes the tool calls in `calls`.
std::string Calls(base::ListValue calls) {
  base::ListValue list;
  for (const base::Value& call : calls) {
    list.Append(
        base::DictValue()
            .Set("id", "call_" + base::NumberToString(list.size()))
            .Set("type", "function")
            .Set("function",
                 base::DictValue()
                     .Set("name", *call.GetDict().FindString("name"))
                     .Set("arguments",
                          *base::WriteJson(*call.GetDict().FindDict("args")))));
  }
  base::ListValue choices;
  choices.Append(base::DictValue().Set(
      "message", base::DictValue()
                     .Set("role", "assistant")
                     .Set("tool_calls", std::move(list))));
  return *base::WriteJson(base::DictValue().Set("choices", std::move(choices)));
}

std::string Answer(std::string_view text) {
  base::ListValue choices;
  choices.Append(base::DictValue().Set(
      "message",
      base::DictValue().Set("role", "assistant").Set("content", text)));
  return *base::WriteJson(base::DictValue().Set("choices", std::move(choices)));
}

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
    embedded_test_server()->RegisterRequestHandler(base::BindRepeating(
        &AgentLoopBrowserTest::ServeModel, base::Unretained(this)));
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

  // The model's answers, in order. Once they run out, it repeats the last.
  void Script(std::vector<std::string> replies, int status = net::HTTP_OK) {
    base::AutoLock lock(lock_);
    replies_.assign(replies.begin(), replies.end());
    status_ = status;
  }

  std::vector<std::string> Requests() {
    base::AutoLock lock(lock_);
    return requests_;
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

  std::vector<AgentLoop::Step> steps_;
  raw_ptr<tabs::TabInterface> chat_;
  std::unique_ptr<AgentLoop> agent_;

 private:
  std::unique_ptr<net::test_server::HttpResponse> ServeModel(
      const net::test_server::HttpRequest& request) {
    if (request.relative_url != "/v1/chat/completions") {
      return nullptr;
    }
    base::AutoLock lock(lock_);
    requests_.push_back(request.content);
    auto response = std::make_unique<net::test_server::BasicHttpResponse>();
    response->set_code(static_cast<net::HttpStatusCode>(status_));
    response->set_content_type("application/json");
    response->set_content(replies_.front());
    if (replies_.size() > 1) {
      replies_.pop_front();
    }
    return response;
  }

  void OnStep(const AgentLoop::Step& step) {
    AgentLoop::Step copy;
    copy.kind = step.kind;
    copy.tool = step.tool;
    copy.args = step.args.Clone();
    copy.result = step.result.Clone();
    copy.text = step.text;
    steps_.push_back(std::move(copy));
  }

  base::Lock lock_;
  std::deque<std::string> replies_ GUARDED_BY(lock_);
  int status_ GUARDED_BY(lock_) = net::HTTP_OK;
  std::vector<std::string> requests_ GUARDED_BY(lock_);
};

IN_PROC_BROWSER_TEST_F(AgentLoopBrowserTest, FillsAFormAndSaysItIsDone) {
  Script({Calls(base::ListValue().Append(
              Call("open", base::DictValue().Set("url", Url("/form"))))),
          Calls(base::ListValue().Append(
              Call("read", base::DictValue().Set("tab", 1)))),
          Calls(base::ListValue()
                    .Append(Call("type", base::DictValue()
                                             .Set("tab", 1)
                                             .Set("element", 1)
                                             .Set("text", "Ada")))
                    .Append(Call("click", base::DictValue().Set("tab", 1).Set(
                                              "element", 2)))),
          Answer("Booked the tour for Ada.")});

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

  const std::vector<std::string> requests = Requests();
  ASSERT_EQ(requests.size(), 4u);
  std::optional<base::DictValue> first =
      base::JSONReader::ReadDict(requests[0], base::JSON_PARSE_RFC);
  ASSERT_TRUE(first);
  EXPECT_EQ(*first->FindString("model"), "test-agent");
  EXPECT_EQ(first->FindList("tools")->size(), 5u);
  const base::ListValue& messages = *first->FindList("messages");
  ASSERT_EQ(messages.size(), 2u);
  EXPECT_EQ(*messages[1].GetDict().FindString("content"),
            "Book a tour for Ada");
  EXPECT_THAT(requests[2], HasSubstr("Your name"));
}

IN_PROC_BROWSER_TEST_F(AgentLoopBrowserTest, AToolThatFailsIsToldToTheModel) {
  Script(
      {Calls(base::ListValue().Append(Call("fly"))), Answer("I can't fly.")});

  const AgentLoop::Step& end = Ask("Fly to Lisbon");

  EXPECT_EQ(end.kind, Kind::kAnswer);
  ASSERT_EQ(steps_.size(), 2u);
  EXPECT_EQ(*steps_[0].result.FindString("error"),
            "There is no tool called fly.");
  EXPECT_THAT(Requests()[1], HasSubstr("There is no tool called fly."));
}

IN_PROC_BROWSER_TEST_F(AgentLoopBrowserTest, SaysWhenTheModelFails) {
  Script({"{}"}, net::HTTP_INTERNAL_SERVER_ERROR);
  EXPECT_EQ(Ask("Hello").text, "The model request failed: HTTP 500");

  Script({"not an answer"});
  EXPECT_EQ(Ask("Hello").text, "The model's answer could not be read.");
}

IN_PROC_BROWSER_TEST_F(AgentLoopBrowserTest, StopsWhenItGoesRoundInCircles) {
  Script({Calls(base::ListValue().Append(Call("fly")))});

  const AgentLoop::Step& end = Ask("Fly to Lisbon");

  EXPECT_EQ(end.kind, Kind::kFailed);
  EXPECT_EQ(end.text, "The agent stopped after 40 steps without an answer.");
  EXPECT_EQ(Requests().size(), 40u);
}

}  // namespace
}  // namespace uw
