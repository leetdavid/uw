// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include <memory>
#include <string>
#include <vector>

#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/run_loop.h"
#include "base/test/run_until.h"
#include "base/threading/thread_restrictions.h"
#include "chrome/browser/actor/actor_keyed_service.h"
#include "chrome/browser/headless/test/headless_mode_browsertest.h"
#include "chrome/browser/prefs/session_startup_pref.h"
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
#include "uw/browser/agent/chat_file.h"
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
    WaitForChat(chat);
    return chat;
  }

  // The chat tab that session restore brought back, once its chat is loaded.
  content::WebContents* RestoredChat() {
    content::WebContents* chat = nullptr;
    EXPECT_TRUE(base::test::RunUntil([&] {
      for (int i = 0; i < tabs()->count(); ++i) {
        if (tabs()->GetWebContentsAt(i)->GetVisibleURL().host() == "chat") {
          chat = tabs()->GetWebContentsAt(i);
        }
      }
      return chat != nullptr;
    }));
    WaitForChat(chat);
    return chat;
  }

  // Waits until the page has its chat and has put the chat's id in its
  // address.
  void WaitForChat(content::WebContents* chat) {
    EXPECT_TRUE(content::WaitForLoadStop(chat));
    const std::string id =
        content::EvalJs(chat,
                        "new Promise(resolve => {"
                        "  const check = () => document.body.dataset.chatId"
                        "      ? resolve(document.body.dataset.chatId)"
                        "      : setTimeout(check, 20);"
                        "  check();"
                        "});")
            .ExtractString();
    EXPECT_TRUE(ChatFile::IsChatId(id));
    EXPECT_TRUE(base::test::RunUntil(
        [&] { return chat->GetLastCommittedURL().path() == "/" + id; }));
  }

  // Runs `script` and returns the next line that ends the agent's turn.
  std::string UntilTheEnd(content::WebContents* chat,
                          const std::string& script) {
    return content::EvalJs(
               chat,
               "const ends = () => document.querySelectorAll('.answer, "
               ".failed');"
               "const before = ends().length;" +
                   script +
                   "new Promise(resolve => {"
                   "  const check = () => ends().length > before"
                   "      ? resolve(ends()[ends().length - 1].textContent)"
                   "      : setTimeout(check, 20);"
                   "  check();"
                   "});")
        .ExtractString();
  }

  // Types `text` into the composer and presses Enter. React owns the field,
  // so the text goes in through an input event, and Enter waits until the
  // page has taken it.
  static std::string SendScript(const std::string& text) {
    return content::JsReplace(
        "const field = document.getElementById('message');"
        "Object.getOwnPropertyDescriptor(HTMLTextAreaElement.prototype,"
        "    'value').set.call(field, $1);"
        "field.dispatchEvent(new Event('input', {bubbles: true}));"
        "setTimeout(() => field.dispatchEvent(new KeyboardEvent('keydown',"
        "    {key: 'Enter', bubbles: true})), 50);",
        text);
  }

  // Sends `text` and returns the line that ends the agent's turn.
  std::string Ask(content::WebContents* chat, const std::string& text) {
    return UntilTheEnd(chat, SendScript(text));
  }

  std::vector<std::string> Lines(content::WebContents* chat,
                                 std::string_view selector) {
    std::vector<std::string> lines;
    for (const base::Value& line :
         content::EvalJs(
             chat, content::JsReplace("[...document.querySelectorAll($1)]"
                                      "    .map(line => line.textContent)",
                                      selector))
             .ExtractList()) {
      lines.push_back(line.GetString());
    }
    return lines;
  }

  std::vector<std::string> Steps(content::WebContents* chat) {
    return Lines(chat, ".step");
  }

  // The id of the chat a page shows, or nothing before the page has one.
  static std::string IdOf(content::WebContents* chat) {
    const std::string_view path = chat->GetLastCommittedURL().path();
    return path.empty() ? std::string() : std::string(path.substr(1));
  }

  // Waits until the chat list in `chat` has `count` rows, then returns their
  // titles from the top.
  std::vector<std::string> ChatList(content::WebContents* chat, int count) {
    EXPECT_TRUE(content::ExecJs(
        chat, content::JsReplace(
                  "new Promise(resolve => {"
                  "  const check = () =>"
                  "      document.querySelectorAll('button[data-chat]').length"
                  "          === $1"
                  "          ? resolve() : setTimeout(check, 20);"
                  "  check();"
                  "});",
                  count)));
    std::vector<std::string> titles;
    for (const base::Value& title :
         content::EvalJs(chat,
                         "[...document.querySelectorAll('button[data-chat]')]"
                         "    .map(row => row.dataset.title)")
             .ExtractList()) {
      titles.push_back(title.GetString());
    }
    return titles;
  }

  // Clicks the element of `chat` that `selector` finds for the chat `id`.
  static void ClickFor(content::WebContents* chat,
                       std::string_view attribute,
                       const std::string& id) {
    EXPECT_TRUE(content::ExecJs(
        chat, content::JsReplace(
                  "document.querySelector('[' + $1 + '=\"' + $2 + '\"]')"
                  "    .click();",
                  attribute, id)));
  }

  std::string Form() {
    return embedded_https_test_server().GetURL("shop.test", "/form").spec();
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

IN_PROC_BROWSER_TEST_F(UwChatBrowserTest, PRE_AChatComesBackAfterARestart) {
  SessionStartupPref::SetStartupPref(
      browser()->GetProfile(), SessionStartupPref(SessionStartupPref::LAST));
  UseAgentModel();
  model_.Script(
      {ScriptedModel::Calls(base::ListValue().Append(
           ScriptedModel::Call("open", base::DictValue().Set("url", Form())))),
       ScriptedModel::Calls(base::ListValue().Append(
           ScriptedModel::Call("read", base::DictValue().Set("tab", 1)))),
       ScriptedModel::Answer("The tour form asks for your name.")});
  content::WebContents* chat = OpenChat();

  ASSERT_EQ(Ask(chat, "What does the tour form ask?"),
            "The tour form asks for your name.");
  // Lets the session take the tabs' data before the browser quits.
  base::RunLoop().RunUntilIdle();
}

IN_PROC_BROWSER_TEST_F(UwChatBrowserTest, AChatComesBackAfterARestart) {
  content::WebContents* chat = RestoredChat();

  EXPECT_EQ(chat->GetTitle(), u"What does the tour form ask?");
  EXPECT_EQ(Lines(chat, ".mine"),
            std::vector<std::string>{"What does the tour form ask?"});
  ASSERT_EQ(Steps(chat).size(), 2u);
  EXPECT_EQ(Steps(chat)[1], "Read Book a tour in tab 1");
  EXPECT_EQ(Lines(chat, ".answer"),
            std::vector<std::string>{"The tour form asks for your name."});

  // The model still knows the chat, with the page it read shortened.
  UseAgentModel();
  model_.Script({ScriptedModel::Answer("Only your name.")});
  EXPECT_EQ(Ask(chat, "Anything else?"), "Only your name.");
  ASSERT_EQ(model_.Requests().size(), 1u);
  EXPECT_THAT(model_.Requests()[0],
              ::testing::HasSubstr("What does the tour form ask?"));
  EXPECT_THAT(model_.Requests()[0],
              ::testing::HasSubstr("Call recall with step 2"));
}

IN_PROC_BROWSER_TEST_F(UwChatBrowserTest, PRE_AStoppedChatGoesOnWithContinue) {
  SessionStartupPref::SetStartupPref(
      browser()->GetProfile(), SessionStartupPref(SessionStartupPref::LAST));
  UseAgentModel();
  model_.Script(
      {ScriptedModel::Calls(base::ListValue().Append(
           ScriptedModel::Call("open", base::DictValue().Set("url", Form())))),
       ScriptedModel::Hang()});
  content::WebContents* chat = OpenChat();

  ASSERT_TRUE(content::ExecJs(chat, SendScript("Book a tour for Ada")));
  // uw quits while the model thinks about its second step.
  ASSERT_TRUE(
      base::test::RunUntil([&] { return model_.Requests().size() == 2; }));
  base::RunLoop().RunUntilIdle();
}

IN_PROC_BROWSER_TEST_F(UwChatBrowserTest, AStoppedChatGoesOnWithContinue) {
  content::WebContents* chat = RestoredChat();
  ASSERT_EQ(Lines(chat, ".stopped"),
            std::vector<std::string>{"Stopped before it finished.Continue"});

  UseAgentModel();
  model_.Script(
      {ScriptedModel::Calls(base::ListValue().Append(ScriptedModel::Call(
           "navigate", base::DictValue().Set("tab", 1).Set("url", Form())))),
       ScriptedModel::Answer("Back on the form.")});
  EXPECT_EQ(UntilTheEnd(chat, "document.getElementById('continue').click();"),
            "Back on the form.");

  // The agent works on in the tab it had before uw quit.
  ASSERT_EQ(Steps(chat).size(), 3u);
  EXPECT_EQ(Steps(chat)[1], "Continued");
  EXPECT_EQ(Steps(chat)[2], "Went to " + Form() + " in tab 1");
  EXPECT_THAT(model_.Requests()[0],
              ::testing::HasSubstr("uw quit while you were working"));
  EXPECT_EQ(Lines(chat, ".stopped"),
            std::vector<std::string>{"Stopped before it finished."});
}

// The page is a React bundle under WebUI's script rules, which refuse HTML
// strings (Trusted Types) and scripts from elsewhere. A broken rule shows as
// a console error.
IN_PROC_BROWSER_TEST_F(UwChatBrowserTest, ThePageRunsWithoutConsoleErrors) {
  UseAgentModel();
  model_.Script({ScriptedModel::Answer("Hello.")});
  content::WebContents* chat = tabs()->GetActiveWebContents();
  content::WebContentsConsoleObserver console(chat);
  console.SetFilter(base::BindRepeating(
      [](const content::WebContentsConsoleObserver::Message& message) {
        return message.log_level == blink::mojom::ConsoleMessageLevel::kError;
      }));
  ASSERT_TRUE(content::NavigateToURL(chat, GURL("chrome://chat/")));
  WaitForChat(chat);

  EXPECT_EQ(Ask(chat, "Hello"), "Hello.");
  EXPECT_TRUE(console.messages().empty()) << console.GetMessageAt(0);
}

IN_PROC_BROWSER_TEST_F(UwChatBrowserTest, TheChatListGoesToAChatsTab) {
  UseAgentModel();
  model_.Script({ScriptedModel::Answer("At noon.")});
  content::WebContents* first = OpenChat();
  ASSERT_EQ(Ask(first, "When does the tour start?"), "At noon.");
  model_.Script({ScriptedModel::Answer("Two hours.")});
  content::WebContents* second = OpenChat();
  ASSERT_EQ(Ask(second, "How long is it?"), "Two hours.");

  EXPECT_EQ(ChatList(second, 2),
            (std::vector<std::string>{"How long is it?",
                                      "When does the tour start?"}));
  ClickFor(second, "data-chat", IdOf(first));
  EXPECT_TRUE(base::test::RunUntil(
      [&] { return tabs()->GetActiveWebContents() == first; }));
}

IN_PROC_BROWSER_TEST_F(UwChatBrowserTest, AClosedChatOpensAgainFromTheList) {
  UseAgentModel();
  model_.Script({ScriptedModel::Answer("At noon.")});
  content::WebContents* first = OpenChat();
  ASSERT_EQ(Ask(first, "When does the tour start?"), "At noon.");
  const std::string id = IdOf(first);
  content::WebContents* second = OpenChat();
  tabs::TabInterface::GetFromContents(first)->Close();

  ASSERT_EQ(ChatList(second, 1),
            std::vector<std::string>{"When does the tour start?"});
  ClickFor(second, "data-chat", id);

  EXPECT_TRUE(base::test::RunUntil(
      [&] { return IdOf(tabs()->GetActiveWebContents()) == id; }));
  content::WebContents* reopened = tabs()->GetActiveWebContents();
  WaitForChat(reopened);
  EXPECT_EQ(Lines(reopened, ".mine"),
            std::vector<std::string>{"When does the tour start?"});
  EXPECT_EQ(Lines(reopened, ".answer"), std::vector<std::string>{"At noon."});
}

IN_PROC_BROWSER_TEST_F(UwChatBrowserTest, DeletingAChatClosesItsTab) {
  UseAgentModel();
  model_.Script({ScriptedModel::Answer("At noon.")});
  content::WebContents* first = OpenChat();
  ASSERT_EQ(Ask(first, "When does the tour start?"), "At noon.");
  const std::string id = IdOf(first);
  model_.Script({ScriptedModel::Answer("Two hours.")});
  content::WebContents* second = OpenChat();
  ASSERT_EQ(Ask(second, "How long is it?"), "Two hours.");
  ASSERT_EQ(ChatList(second, 2).size(), 2u);
  const int before = tabs()->count();

  ClickFor(second, "data-delete", id);

  EXPECT_TRUE(
      base::test::RunUntil([&] { return tabs()->count() == before - 1; }));
  EXPECT_EQ(ChatList(second, 1), std::vector<std::string>{"How long is it?"});
  const base::FilePath file = browser()
                                  ->GetProfile()
                                  ->GetPath()
                                  .AppendASCII("uw_chats")
                                  .AppendASCII(id + ".json");
  EXPECT_TRUE(base::test::RunUntil([&] {
    base::ScopedAllowBlockingForTesting allow_blocking;
    return !base::PathExists(file);
  }));
}

IN_PROC_BROWSER_TEST_F(UwChatBrowserTest, TabCardsShowTheAgentsTabs) {
  UseAgentModel();
  model_.Script(
      {ScriptedModel::Calls(base::ListValue().Append(
           ScriptedModel::Call("open", base::DictValue().Set("url", Form())))),
       ScriptedModel::Calls(base::ListValue().Append(
           ScriptedModel::Call("read", base::DictValue().Set("tab", 1)))),
       ScriptedModel::Calls(base::ListValue().Append(ScriptedModel::Call(
           "click", base::DictValue().Set("tab", 1).Set("element", 2)))),
       ScriptedModel::Answer("Clicked Send.")});
  content::WebContents* chat = OpenChat();
  ASSERT_EQ(Ask(chat, "Click Send"), "Clicked Send.");

  // Each step leaves a picture of the tab on its card.
  EXPECT_TRUE(content::ExecJs(
      chat,
      "new Promise(resolve => {"
      "  const check = () => document.querySelector("
      "      '.tab-card[data-tab=\"1\"] img[src^=\"data:image/jpeg\"]')"
      "      ? resolve() : setTimeout(check, 20);"
      "  check();"
      "});"));
  SaveWindowScreenshot(browser(), "chat-cards");
  EXPECT_EQ(Lines(chat, ".tab-title"), std::vector<std::string>{"Book a tour"});
  EXPECT_EQ(Lines(chat, ".tab-doing"),
            std::vector<std::string>{"Clicked element 2 in tab 1"});

  ClickFor(chat, "data-tab", "1");
  EXPECT_TRUE(base::test::RunUntil([&] {
    return tabs()->GetActiveWebContents()->GetLastCommittedURL().spec() ==
           Form();
  }));
}

IN_PROC_BROWSER_TEST_F(UwChatBrowserTest, SideBySidePutsTheChatBesideItsTab) {
  UseAgentModel();
  model_.Script(
      {ScriptedModel::Calls(base::ListValue().Append(
           ScriptedModel::Call("open", base::DictValue().Set("url", Form())))),
       ScriptedModel::Answer("Opened the form.")});
  content::WebContents* chat = OpenChat();
  ASSERT_EQ(Ask(chat, "Open the form"), "Opened the form.");
  tabs::TabInterface* chat_tab = tabs::TabInterface::GetFromContents(chat);
  tabs::TabInterface* agent_tab = nullptr;
  for (int i = 0; i < tabs()->count(); ++i) {
    if (tabs()->GetWebContentsAt(i)->GetLastCommittedURL().spec() == Form()) {
      agent_tab = tabs()->GetTabAtIndex(i);
    }
  }
  ASSERT_TRUE(agent_tab);

  ASSERT_TRUE(content::ExecJs(
      chat, "document.querySelector('[data-side-by-side]').click();"));
  EXPECT_TRUE(base::test::RunUntil([&] {
    return chat_tab->GetSplit().has_value() &&
           agent_tab->GetSplit() == chat_tab->GetSplit();
  }));
  SaveWindowScreenshot(browser(), "side-by-side");

  ASSERT_TRUE(content::ExecJs(
      chat, "document.querySelector('[data-side-by-side]').click();"));
  EXPECT_TRUE(
      base::test::RunUntil([&] { return !chat_tab->GetSplit().has_value(); }));
}

IN_PROC_BROWSER_TEST_F(UwChatBrowserTest, WithoutAnAgentModelItSaysWhatToDo) {
  content::WebContents* chat = OpenChat();

  EXPECT_EQ(Ask(chat, "Book a tour for Ada"),
            "Set an agent model on uw's settings page first.");
  EXPECT_TRUE(model_.Requests().empty());
}

}  // namespace
}  // namespace uw
