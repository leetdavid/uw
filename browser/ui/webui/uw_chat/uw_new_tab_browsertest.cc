// Copyright 2026 The uw Authors. All rights reserved.

#include <memory>
#include <string>
#include <vector>

#include "base/files/file_util.h"
#include "base/json/json_writer.h"
#include "base/strings/strcat.h"
#include "base/test/run_until.h"
#include "base/threading/thread_restrictions.h"
#include "base/time/time.h"
#include "base/values.h"
#include "chrome/browser/actor/actor_keyed_service.h"
#include "chrome/browser/headless/test/headless_mode_browsertest.h"
#include "chrome/browser/notifications/notification_display_service_tester.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/tabs/tab_enums.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/common/webui_url_constants.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/prefs/pref_service.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "net/dns/mock_host_resolver.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "net/test/embedded_test_server/http_request.h"
#include "net/test/embedded_test_server/http_response.h"
#include "url/gurl.h"
#include "uw/browser/agent/chat_alerts.h"
#include "uw/browser/agent/chat_file.h"
#include "uw/browser/agent/chat_index.h"
#include "uw/browser/agent/chat_tab.h"
#include "uw/browser/agent/scheduled_chats.h"
#include "uw/browser/agent/scripted_model.h"
#include "uw/browser/ai_providers/scoped_memory_keychain.h"
#include "uw/browser/tab_organization/window_screenshot.h"
#include "uw/browser/ui/views/tab_organization/inbox_button.h"
#include "uw/browser/ui/webui/uw_chat/uw_scheduled_ui.h"
#include "uw/components/ai_providers/providers.h"

namespace uw {
namespace {

constexpr char kDesk[] = R"(<!doctype html>
<title>Jarvis Bamboo Standing Desk</title>
<p>Price: $479</p>)";

InboxButton* FindInboxButton(views::View* view) {
  if (auto* button = views::AsViewClass<InboxButton>(view)) {
    return button;
  }
  for (views::View* child : view->children()) {
    if (InboxButton* found = FindInboxButton(child)) {
      return found;
    }
  }
  return nullptr;
}

// Finds an element of the page, waiting for it to show.
constexpr char kUntil[] = R"(
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
  const $ = (selector) => document.querySelector(selector);
  const type = (text) => {
    const field = $('#newtab-field');
    const setter = Object.getOwnPropertyDescriptor(
        Object.getPrototypeOf(field), 'value').set;
    setter.call(field, text);
    field.dispatchEvent(new Event('input', {bubbles: true}));
  };
  const press = (key) => $('#newtab-field').dispatchEvent(
      new KeyboardEvent('keydown', {key, bubbles: true}));
)";

// uw's new tab page in every new tab, its box, and what the box does. See
// docs/plans/new-tab-page.md.
class NewTabBrowserTest : public headless::HeadlessModeBrowserTest {
 protected:
  void SetUpOnMainThread() override {
    HeadlessModeBrowserTest::SetUpOnMainThread();
    host_resolver()->AddRule("fully.test", "127.0.0.1");
    embedded_https_test_server().SetCertHostnames({"fully.test"});
    embedded_https_test_server().RegisterRequestHandler(base::BindRepeating(
        [](const net::test_server::HttpRequest& request)
            -> std::unique_ptr<net::test_server::HttpResponse> {
          if (!request.relative_url.starts_with("/desk")) {
            return nullptr;
          }
          auto response =
              std::make_unique<net::test_server::BasicHttpResponse>();
          response->set_content_type("text/html");
          response->set_content(kDesk);
          return response;
        }));
    ASSERT_TRUE(embedded_https_test_server().Start());
    ASSERT_TRUE(embedded_test_server()->Start());
    notifications_ = std::make_unique<NotificationDisplayServiceTester>(
        browser()->GetProfile());
  }

  void TearDownOnMainThread() override {
    notifications_.reset();
    actor::ActorKeyedService::Get(browser()->GetProfile())->ResetForTesting();
    HeadlessModeBrowserTest::TearDownOnMainThread();
  }

  std::string UseAgentModel() {
    PrefService& prefs = *browser()->GetProfile()->GetPrefs();
    Provider provider = PresetProvider(ProviderPreset::kCustom);
    provider.name = "Test server";
    provider.address = embedded_test_server()->GetURL("/v1").spec();
    const std::string id = AddProvider(prefs, provider);
    SetChoice(prefs, ModelSlot::kAgent, id, "test-agent");
    return id;
  }

  GURL Desk() {
    return embedded_https_test_server().GetURL("fully.test", "/desk");
  }
  ScheduledChats* chats() {
    return ScheduledChats::For(browser()->GetProfile());
  }
  ChatAlerts* alerts() { return ChatAlerts::For(browser()->GetProfile()); }
  ChatIndex* index() { return ChatIndex::For(browser()->GetProfile()); }

  // Watches the desk, daily at 9:00, with a first run that finds its price
  // below $500, so a Change alert waits unseen.
  std::string WatchTheDesk() {
    const std::string provider = UseAgentModel();
    model_.Script(
        {ScriptedModel::Calls(base::ListValue().Append(ScriptedModel::Call(
             "report", base::DictValue()
                           .Set("found", true)
                           .Set("message", "$479, down $70")
                           .Set("values", base::ListValue().Append(
                                              base::DictValue()
                                                  .Set("name", "price")
                                                  .Set("unit", "USD")
                                                  .Set("number", 479)))))),
         ScriptedModel::Answer("Reported.")});
    ScheduledChat watch;
    watch.request = "Tell me when the price of the desk drops below $500.";
    watch.page = Desk().spec();
    watch.watch_for = "Price drops below $500";
    watch.provider_id = provider;
    watch.model = "test-agent";
    const std::string id = chats()->Watch(watch);
    EXPECT_TRUE(base::test::RunUntil([&] { return !chats()->Running(id); }));
    EXPECT_TRUE(base::test::RunUntil([&] { return alerts()->Unseen() == 1; }));
    return id;
  }

  // A chat whose agent answered while no one looked, kept as uw keeps it.
  std::string AddUnreadChat() {
    const std::string id = ChatFile::NewChatId();
    const base::DictValue chat =
        base::DictValue()
            .Set("version", 1)
            .Set("title", "Compare Lisbon hotels")
            .Set("log",
                 base::ListValue()
                     .Append(base::DictValue()
                                 .Set("kind", "user")
                                 .Set("text", "Which hotel fits?"))
                     .Append(base::DictValue()
                                 .Set("kind", "answer")
                                 .Set("text", "Memmo Alfama fits your dates")));
    {
      base::ScopedAllowBlockingForTesting allow_blocking;
      const base::FilePath folder =
          ChatFile::FolderFor(browser()->GetProfile()->GetPath());
      EXPECT_TRUE(base::CreateDirectory(folder));
      EXPECT_TRUE(
          base::WriteFile(folder.AppendASCII(base::StrCat({id, ".json"})),
                          *base::WriteJson(chat)));
    }
    index()->Update({id, "Compare Lisbon hotels", base::Time::Now(),
                     ChatIndex::State::kUnread});
    return id;
  }

  InboxButton* inbox() {
    return FindInboxButton(BrowserView::GetBrowserViewForBrowser(browser())
                               ->GetWidget()
                               ->GetRootView());
  }

  GURL ActiveUrl() {
    return browser()
        ->tab_strip_model()
        ->GetActiveWebContents()
        ->GetLastCommittedURL();
  }

  // Opens a blank tab in `window`, as Cmd+T does with the command bar off.
  content::WebContents* OpenNewTab(Browser* window) {
    chrome::NewTab(window, NewTabTypes::kNewTabCommand);
    content::WebContents* tab =
        window->tab_strip_model()->GetActiveWebContents();
    EXPECT_TRUE(content::WaitForLoadStop(tab));
    return tab;
  }

  static content::EvalJsResult Run(content::WebContents* tab,
                                   const std::string& script) {
    return content::EvalJs(
        tab, base::StrCat({"(async () => {", kUntil, script, "})()"}));
  }

  ScopedMemoryKeychain keychain_;
  ScriptedModel model_{*embedded_test_server()};
  std::unique_ptr<NotificationDisplayServiceTester> notifications_;
};

IN_PROC_BROWSER_TEST_F(NewTabBrowserTest, ANewTabShowsUwsPage) {
  content::WebContents* tab = OpenNewTab(browser());
  EXPECT_EQ(tab->GetPrimaryMainFrame()->GetLastCommittedURL().host(),
            chrome::kChromeUINewTabPageHost);
  // The address bar shows nothing, as on Chromium's new tab page.
  EXPECT_EQ(tab->GetController().GetLastCommittedEntry()->GetVirtualURL(),
            GURL(chrome::kChromeUINewTabURL));
  EXPECT_EQ(true, Run(tab, "return !!(await until(() => $('#newtab-box')));"));
  EXPECT_EQ(true, Run(tab, "return !$('#newtab-private');"));
  SaveWindowScreenshot(browser(), "new_tab_page");
}

IN_PROC_BROWSER_TEST_F(NewTabBrowserTest, APrivateWindowShowsOnlyTheBox) {
  UseAgentModel();
  AddUnreadChat();
  Browser* incognito = CreateIncognitoBrowser();
  content::WebContents* tab = OpenNewTab(incognito);
  EXPECT_EQ(tab->GetPrimaryMainFrame()->GetLastCommittedURL().host(),
            chrome::kChromeUINewTabPageHost);
  EXPECT_EQ(tab->GetController().GetLastCommittedEntry()->GetVirtualURL(),
            GURL(chrome::kChromeUINewTabURL));
  EXPECT_EQ(true,
            Run(tab, "return !!(await until(() => $('#newtab-private')));"));
  // Ask has no Once chip there: a private chat never runs on a schedule.
  EXPECT_EQ(true, Run(tab,
                      "$('#newtab-ask').click();"
                      "await until(() => $('#newtab-send'));"
                      "return !$('#newtab-once');"));
  // Nothing of the profile's chats shows there.
  EXPECT_EQ(true, Run(tab, "return !$('#newtab-finished');"));
  SaveWindowScreenshot(incognito, "new_tab_page_private");
}

IN_PROC_BROWSER_TEST_F(NewTabBrowserTest, AnAddressOpensHere) {
  const GURL page = embedded_test_server()->GetURL("/title2.html");
  content::WebContents* tab = OpenNewTab(browser());
  const int tabs = browser()->tab_strip_model()->count();
  ASSERT_EQ(true, Run(tab, base::StrCat({
                               "await until(() => $('#newtab-field'));"
                               "type('",
                               page.spec(),
                               "');"
                               "return !!(await until(() =>"
                               "    $('#newtab-rows li')));",
                           })));
  ASSERT_EQ(true, Run(tab, "press('Enter'); return true;"));
  EXPECT_TRUE(base::test::RunUntil([&] {
    return browser()
               ->tab_strip_model()
               ->GetActiveWebContents()
               ->GetLastCommittedURL() == page;
  }));
  EXPECT_EQ(browser()->tab_strip_model()->count(), tabs);
}

IN_PROC_BROWSER_TEST_F(NewTabBrowserTest, AskTurnsThisTabIntoAChat) {
  UseAgentModel();
  content::WebContents* tab = OpenNewTab(browser());
  const int tabs = browser()->tab_strip_model()->count();
  ASSERT_EQ(true, Run(tab,
                      "(await until(() => $('#newtab-ask'))).click();"
                      "await until(() => $('textarea#newtab-field'));"
                      "type('Find a tram day pass');"
                      "press('Enter');"
                      "return true;"));
  EXPECT_TRUE(base::test::RunUntil([&] {
    return browser()
               ->tab_strip_model()
               ->GetActiveWebContents()
               ->GetLastCommittedURL()
               .host() == "chat";
  }));
  EXPECT_EQ(browser()->tab_strip_model()->count(), tabs);
  content::WebContents* chat =
      browser()->tab_strip_model()->GetActiveWebContents();
  EXPECT_EQ(true, Run(chat,
                      "return !!(await until(() => document.body.innerText"
                      "    .includes('Find a tram day pass')));"));
}

IN_PROC_BROWSER_TEST_F(NewTabBrowserTest, ARunShowsUnderFinishedAndComingUp) {
  const std::string id = WatchTheDesk();
  content::WebContents* tab = OpenNewTab(browser());
  const std::string row = base::StrCat({"[data-chat=\"", id, "\"]"});
  EXPECT_EQ(true, Run(tab, base::StrCat({
                               "const row = await until(() =>"
                               "    $('#newtab-finished ",
                               row,
                               "'));"
                               "return row.textContent.includes("
                               "    'Change alert · $479, down $70');",
                           })));
  EXPECT_EQ(true, Run(tab, base::StrCat({
                               "const row = await until(() =>"
                               "    $('#newtab-coming-up ",
                               row,
                               "'));"
                               "return row.textContent.includes("
                               "    'Price drops below $500 · every day');",
                           })));
  SaveWindowScreenshot(browser(), "new_tab_page_sections");

  // × takes the row off; the Inbox keeps the alert.
  EXPECT_EQ(true, Run(tab,
                      "$('[data-dismiss]').click();"
                      "return !!(await until(() =>"
                      "    !$('#newtab-finished')));"));
  EXPECT_EQ(alerts()->Unseen(), 0u);
  EXPECT_EQ(alerts()->alerts().size(), 1u);

  // A run that could not sign in waits for the user, and Open page opens the
  // page in this tab.
  alerts()->Add(id, "Sign in required", "fully.test", /*waiting=*/false);
  const int tabs = browser()->tab_strip_model()->count();
  EXPECT_EQ(true, Run(tab, base::StrCat({
                               "const row = await until(() =>"
                               "    $('#newtab-needs-you ",
                               row,
                               "'));"
                               "if (!row.textContent.includes("
                               "    'Sign in required · fully.test')) {"
                               "  return false;"
                               "}"
                               "row.querySelector('button').click();"
                               "return true;",
                           })));
  EXPECT_TRUE(base::test::RunUntil([&] { return ActiveUrl() == Desk(); }));
  EXPECT_EQ(browser()->tab_strip_model()->count(), tabs);
  EXPECT_EQ(alerts()->Unseen(), 0u);
}

IN_PROC_BROWSER_TEST_F(NewTabBrowserTest, AChatsAnswerShowsUntilItIsSeen) {
  const std::string id = AddUnreadChat();
  content::WebContents* tab = OpenNewTab(browser());
  const std::string row = base::StrCat({"[data-chat=\"", id, "\"]"});
  EXPECT_EQ(true,
            Run(tab, base::StrCat({
                         "const row = await until(() =>"
                         "    $('#newtab-finished ",
                         row,
                         "'));"
                         "return row.textContent.includes("
                         "    'Answered · Memmo Alfama fits your dates');",
                     })));
  EXPECT_EQ(true, Run(tab,
                      "$('[data-dismiss]').click();"
                      "return !!(await until(() =>"
                      "    !$('#newtab-finished')));"));
  EXPECT_EQ(index()->Entries().front().state, ChatIndex::State::kDone);

  // A chat that waits for the user opens from Review, in this tab.
  index()->Update({id, "Compare Lisbon hotels", base::Time::Now(),
                   ChatIndex::State::kWaiting});
  EXPECT_EQ(true, Run(tab, base::StrCat({
                               "const row = await until(() =>"
                               "    $('#newtab-needs-you ",
                               row,
                               "'));"
                               "row.querySelector('button').click();"
                               "return true;",
                           })));
  EXPECT_TRUE(base::test::RunUntil([&] {
    const std::vector<tabs::TabInterface*> showing =
        TabsShowingChat(browser()->GetProfile(), id);
    return showing.size() == 1 && showing[0]->GetContents() == tab;
  }));
}

IN_PROC_BROWSER_TEST_F(NewTabBrowserTest,
                       TheSectionsLinksOpenTheInboxAndScheduled) {
  WatchTheDesk();
  content::WebContents* tab = OpenNewTab(browser());
  ASSERT_TRUE(inbox());
  EXPECT_EQ(true, Run(tab,
                      "(await until(() => $('#newtab-inbox'))).click();"
                      "return true;"));
  EXPECT_TRUE(base::test::RunUntil([&] { return inbox()->IsShowing(); }));

  const int tabs = browser()->tab_strip_model()->count();
  EXPECT_EQ(true, Run(tab,
                      "(await until(() => $('#newtab-scheduled'))).click();"
                      "return true;"));
  EXPECT_TRUE(
      base::test::RunUntil([&] { return ActiveUrl() == GURL(kScheduledUrl); }));
  EXPECT_EQ(browser()->tab_strip_model()->count(), tabs);
}

}  // namespace
}  // namespace uw
