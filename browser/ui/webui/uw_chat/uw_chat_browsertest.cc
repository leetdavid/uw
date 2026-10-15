// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include <memory>
#include <string>
#include <vector>

#include "base/functional/bind.h"
#include "chrome/browser/actor/actor_keyed_service.h"
#include "chrome/browser/headless/test/headless_mode_browsertest.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/toolbar/toolbar_view.h"
#include "components/prefs/pref_service.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "net/dns/mock_host_resolver.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "net/test/embedded_test_server/http_request.h"
#include "net/test/embedded_test_server/http_response.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "uw/browser/agent/chat_tab.h"
#include "uw/browser/agent/scripted_model.h"
#include "uw/browser/tab_organization/ai_connection.h"
#include "uw/browser/tab_organization/tab_organization_controller.h"
#include "uw/browser/tab_organization/window_screenshot.h"

namespace uw {
namespace {

constexpr char kForm[] = R"(<!doctype html>
<title>Book a tour</title>
<label for="name">Your name</label>
<input id="name" name="name">
<button onclick="document.getElementById('said').textContent =
    document.getElementById('name').value">Send</button>
<p id="said"></p>)";

std::unique_ptr<net::test_server::HttpResponse> ServeForm(
    const net::test_server::HttpRequest& request) {
  if (request.relative_url != "/form") {
    return nullptr;
  }
  auto response = std::make_unique<net::test_server::BasicHttpResponse>();
  response->set_content_type("text/html");
  response->set_content(kForm);
  return response;
}

// Drives the real uw://chat page against a scripted model.
class UwChatBrowserTest : public headless::HeadlessModeBrowserTest {
 protected:
  void SetUpOnMainThread() override {
    HeadlessModeBrowserTest::SetUpOnMainThread();
    host_resolver()->AddRule("shop.test", "127.0.0.1");
    embedded_https_test_server().SetCertHostnames({"shop.test"});
    embedded_https_test_server().RegisterRequestHandler(
        base::BindRepeating(&ServeForm));
    ASSERT_TRUE(embedded_https_test_server().Start());
    ASSERT_TRUE(embedded_test_server()->Start());
  }

  void TearDownOnMainThread() override {
    actor::ActorKeyedService::Get(browser()->GetProfile())->ResetForTesting();
    HeadlessModeBrowserTest::TearDownOnMainThread();
  }

  TabStripModel* tabs() { return browser()->tab_strip_model(); }

  void UseAgentModel() {
    PrefService* prefs = browser()->GetProfile()->GetPrefs();
    prefs->SetInteger(kAgentModePref,
                      static_cast<int>(ConnectionMode::kOllama));
    prefs->SetString(kAgentEndpointPref,
                     embedded_test_server()->GetURL("/v1").spec());
    prefs->SetString(kAgentModelPref, "test-agent");
  }

  content::WebContents* OpenChat() {
    chrome::ExecuteCommand(browser(), kNewChatCommand);
    content::WebContents* chat = tabs()->GetActiveWebContents();
    EXPECT_TRUE(content::WaitForLoadStop(chat));
    EXPECT_EQ(chat->GetLastCommittedURL().spec(), "chrome://chat/");
    return chat;
  }

  // Sends `text` and returns the line that ends the agent's turn.
  std::string Ask(content::WebContents* chat, const std::string& text) {
    return content::EvalJs(
               chat,
               content::JsReplace(
                   "document.getElementById('message').value = $1;"
                   "document.getElementById('composer').requestSubmit();"
                   "new Promise(resolve => {"
                   "  const check = () => {"
                   "    const end = document.querySelector('.answer, .failed');"
                   "    end ? resolve(end.textContent) : setTimeout(check, 20);"
                   "  };"
                   "  check();"
                   "});",
                   text))
        .ExtractString();
  }

  std::vector<std::string> Steps(content::WebContents* chat) {
    std::vector<std::string> steps;
    for (const base::Value& step :
         content::EvalJs(chat,
                         "[...document.querySelectorAll('.step')]"
                         "    .map(line => line.textContent)")
             .ExtractList()) {
      steps.push_back(step.GetString());
    }
    return steps;
  }

  ScriptedModel model_{*embedded_test_server()};
};

IN_PROC_BROWSER_TEST_F(UwChatBrowserTest, FillsAFormInATabUnderTheChat) {
  UseAgentModel();
  const std::string form =
      embedded_https_test_server().GetURL("shop.test", "/form").spec();
  model_.Script(
      {ScriptedModel::Calls(base::ListValue().Append(
           ScriptedModel::Call("open", base::DictValue().Set("url", form)))),
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
  content::WebContents* chat = OpenChat();

  EXPECT_EQ(Ask(chat, "Book a tour for Ada"), "Booked the tour for Ada.");
  SaveWindowScreenshot(browser(), "chat");

  EXPECT_EQ(
      Steps(chat),
      (std::vector<std::string>{
          "Opened " + form + " in tab 1", "Read Book a tour in tab 1",
          "Typed into element 1 in tab 1", "Clicked element 2 in tab 1"}));
  EXPECT_EQ(chat->GetTitle(), u"Book a tour for Ada");
  EXPECT_EQ(tabs()->GetActiveWebContents(), chat);
  tabs::TabInterface* agent_tab = nullptr;
  for (int i = 0; i < tabs()->count(); ++i) {
    if (tabs()->GetWebContentsAt(i)->GetLastCommittedURL().spec() == form) {
      agent_tab = tabs()->GetTabAtIndex(i);
    }
  }
  ASSERT_TRUE(agent_tab);
  EXPECT_EQ(content::EvalJs(agent_tab->GetContents(),
                            "document.getElementById('said').textContent"),
            "Ada");
  TabOrganizationController* tree = TabOrganizationController::From(browser());
  ASSERT_TRUE(tree);
  EXPECT_EQ(tree->tree().Find(*tree->IdFor(agent_tab))->parent,
            *tree->IdFor(tabs::TabInterface::GetFromContents(chat)));
}

// Chromium's task icon brings Gemini's button with it, and Gemini is not
// available in uw: clicking that button aborted the browser.
IN_PROC_BROWSER_TEST_F(UwChatBrowserTest, ARunningChatShowsNoGeminiButton) {
  UseAgentModel();
  const std::string form =
      embedded_https_test_server().GetURL("shop.test", "/form").spec();
  model_.Script(
      {ScriptedModel::Calls(base::ListValue().Append(
           ScriptedModel::Call("open", base::DictValue().Set("url", form)))),
       ScriptedModel::Calls(base::ListValue().Append(
           ScriptedModel::Call("read", base::DictValue().Set("tab", 1)))),
       ScriptedModel::Calls(base::ListValue().Append(ScriptedModel::Call(
           "click", base::DictValue().Set("tab", 1).Set("element", 2)))),
       ScriptedModel::Answer("Clicked Send.")});
  content::WebContents* chat = OpenChat();

  EXPECT_EQ(Ask(chat, "Click Send"), "Clicked Send.");

  views::LabelButton* gemini = BrowserView::GetBrowserViewForBrowser(browser())
                                   ->toolbar()
                                   ->GetGlicButton();
  EXPECT_FALSE(gemini && gemini->GetVisible());
}

IN_PROC_BROWSER_TEST_F(UwChatBrowserTest, WithoutAnAgentModelItSaysWhatToDo) {
  content::WebContents* chat = OpenChat();

  EXPECT_EQ(Ask(chat, "Book a tour for Ada"),
            "Set an agent model on uw's settings page first.");
  EXPECT_TRUE(model_.Requests().empty());
}

}  // namespace
}  // namespace uw
