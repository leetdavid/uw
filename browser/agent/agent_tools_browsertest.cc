// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/browser/agent/agent_tools.h"

#include <memory>
#include <string>
#include <string_view>

#include "base/functional/bind.h"
#include "base/test/test_future.h"
#include "chrome/browser/actor/actor_keyed_service.h"
#include "chrome/browser/headless/test/headless_mode_browsertest.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "net/dns/mock_host_resolver.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "net/test/embedded_test_server/http_request.h"
#include "net/test/embedded_test_server/http_response.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "uw/browser/tab_organization/tab_organization_controller.h"

namespace uw {
namespace {

using ::testing::HasSubstr;

constexpr char kArticle[] = R"(<!doctype html>
<title>Lisbon guide</title>
<h1>Three days in Lisbon</h1>
<p>Start in Alfama, early.</p>
<a href="/form">Book a tour</a>)";

constexpr char kForm[] = R"(<!doctype html>
<title>Book a tour</title>
<label for="name">Your name</label>
<input id="name" name="name">
<button onclick="document.getElementById('said').textContent =
    document.getElementById('name').value">Send</button>
<p id="said"></p>)";

std::unique_ptr<net::test_server::HttpResponse> Serve(
    const net::test_server::HttpRequest& request) {
  auto response = std::make_unique<net::test_server::BasicHttpResponse>();
  response->set_content_type("text/html");
  if (request.relative_url == "/article") {
    response->set_content(kArticle);
  } else if (request.relative_url == "/form") {
    response->set_content(kForm);
  } else {
    return nullptr;
  }
  return response;
}

class AgentToolsBrowserTest : public headless::HeadlessModeBrowserTest {
 protected:
  void SetUpOnMainThread() override {
    HeadlessModeBrowserTest::SetUpOnMainThread();
    host_resolver()->AddRule("shop.test", "127.0.0.1");
    embedded_https_test_server().SetCertHostnames({"shop.test"});
    embedded_https_test_server().RegisterRequestHandler(
        base::BindRepeating(&Serve));
    ASSERT_TRUE(embedded_https_test_server().Start());
    chat_ = tabs()->GetActiveTab();
    tools_ = std::make_unique<AgentTools>(*chat_);
  }

  void TearDownOnMainThread() override {
    tools_.reset();
    // Each task's engine holds the profile, which must outlive it.
    actor::ActorKeyedService::Get(browser()->GetProfile())->ResetForTesting();
    HeadlessModeBrowserTest::TearDownOnMainThread();
  }

  TabStripModel* tabs() { return browser()->tab_strip_model(); }

  std::string Url(std::string_view path) {
    return embedded_https_test_server().GetURL("shop.test", path).spec();
  }

  base::DictValue Call(std::string_view tool, base::DictValue args) {
    base::test::TestFuture<base::DictValue> done;
    tools_->Call(tool, args, done.GetCallback());
    return done.Take();
  }

  // Opens `path` for the agent and returns the number of its tab.
  int Open(std::string_view path) {
    const base::DictValue opened =
        Call("open", base::DictValue().Set("url", Url(path)));
    EXPECT_FALSE(opened.FindString("error")) << *opened.FindString("error");
    return opened.FindInt("tab").value_or(0);
  }

  // The tab the agent opened last.
  tabs::TabInterface* AgentTab() {
    for (int i = 0; i < tabs()->count(); ++i) {
      if (tabs()->GetTabAtIndex(i) != chat_) {
        return tabs()->GetTabAtIndex(i);
      }
    }
    return nullptr;
  }

  raw_ptr<tabs::TabInterface> chat_;
  std::unique_ptr<AgentTools> tools_;
};

IN_PROC_BROWSER_TEST_F(AgentToolsBrowserTest,
                       OpensAPageUnderTheChatInTheBackground) {
  EXPECT_EQ(Open("/article"), 1);

  tabs::TabInterface* tab = AgentTab();
  ASSERT_TRUE(tab);
  EXPECT_EQ(tab->GetContents()->GetLastCommittedURL().spec(), Url("/article"));
  EXPECT_EQ(tabs()->GetActiveTab(), chat_);
  TabOrganizationController* tree = TabOrganizationController::From(browser());
  ASSERT_TRUE(tree);
  EXPECT_EQ(tree->tree().Find(*tree->IdFor(tab))->parent, *tree->IdFor(chat_));
}

IN_PROC_BROWSER_TEST_F(AgentToolsBrowserTest, ReadsAPageWithItsLinksNumbered) {
  const int tab = Open("/article");

  const base::DictValue page = Call("read", base::DictValue().Set("tab", tab));

  ASSERT_FALSE(page.FindString("error")) << *page.FindString("error");
  EXPECT_EQ(*page.FindString("title"), "Lisbon guide");
  EXPECT_EQ(*page.FindString("url"), Url("/article"));
  const std::string& text = *page.FindString("text");
  EXPECT_THAT(text, HasSubstr("# Three days in Lisbon\n"));
  EXPECT_THAT(text, HasSubstr("Start in Alfama, early.\n"));
  EXPECT_THAT(text, HasSubstr("[1] link \"Book a tour\" to " + Url("/form")));
}

IN_PROC_BROWSER_TEST_F(AgentToolsBrowserTest, FillsAFormByTheNumbersItRead) {
  const int tab = Open("/form");
  const base::DictValue page = Call("read", base::DictValue().Set("tab", tab));
  ASSERT_FALSE(page.FindString("error")) << *page.FindString("error");
  const std::string& text = *page.FindString("text");
  ASSERT_THAT(text, HasSubstr("[1] text field \"Your name\"")) << text;
  ASSERT_THAT(text, HasSubstr("[2] button \"Send\"")) << text;

  const base::DictValue typed = Call(
      "type",
      base::DictValue().Set("tab", tab).Set("element", 1).Set("text", "Ada"));
  EXPECT_FALSE(typed.FindString("error")) << *typed.FindString("error");
  const base::DictValue clicked =
      Call("click", base::DictValue().Set("tab", tab).Set("element", 2));
  EXPECT_FALSE(clicked.FindString("error")) << *clicked.FindString("error");

  EXPECT_EQ(content::EvalJs(AgentTab()->GetContents(),
                            "document.getElementById('said').textContent"),
            "Ada");
  EXPECT_EQ(tabs()->GetActiveTab(), chat_);
}

IN_PROC_BROWSER_TEST_F(AgentToolsBrowserTest, SaysWhatWentWrong) {
  const int tab = Open("/form");

  EXPECT_THAT(
      *Call("click", base::DictValue().Set("tab", tab).Set("element", 1))
           .FindString("error"),
      HasSubstr("Read the tab again"));
  EXPECT_THAT(
      *Call("read", base::DictValue().Set("tab", 9)).FindString("error"),
      HasSubstr("not one of the agent's tabs"));
  EXPECT_THAT(*Call("open", base::DictValue().Set("url", "file:///etc/hosts"))
                   .FindString("error"),
              HasSubstr("http or https"));
  EXPECT_EQ(*Call("fly", base::DictValue()).FindString("error"),
            "There is no tool called fly.");
}

}  // namespace
}  // namespace uw
