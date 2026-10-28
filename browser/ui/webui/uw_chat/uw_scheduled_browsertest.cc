// Copyright 2026 The uw Authors. All rights reserved.

#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "base/json/values_util.h"
#include "base/strings/strcat.h"
#include "base/test/run_until.h"
#include "base/test/test_future.h"
#include "chrome/browser/actor/actor_keyed_service.h"
#include "chrome/browser/headless/test/headless_mode_browsertest.h"
#include "chrome/browser/notifications/notification_display_service_tester.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/test/base/ui_test_utils.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "net/dns/mock_host_resolver.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "net/test/embedded_test_server/http_request.h"
#include "net/test/embedded_test_server/http_response.h"
#include "uw/browser/agent/chat_file.h"
#include "uw/browser/agent/chat_index.h"
#include "uw/browser/agent/chat_tab.h"
#include "uw/browser/agent/scheduled_chats.h"
#include "uw/browser/agent/scripted_model.h"
#include "uw/browser/ai_providers/scoped_memory_keychain.h"
#include "uw/browser/tab_organization/commands.h"
#include "uw/browser/tab_organization/window_screenshot.h"
#include "uw/browser/ui/webui/uw_chat/uw_scheduled_ui.h"
#include "uw/components/ai_providers/providers.h"

namespace uw {
namespace {

constexpr char kDesk[] = R"(<!doctype html>
<title>Jarvis Bamboo Standing Desk</title>
<p>Price: $549</p>)";

// A run's line as a chat file keeps it: one that reported `price`, or one
// that didn't finish.
base::DictValue SavedRun(base::Time time,
                         std::optional<double> price,
                         bool found = false) {
  base::DictValue line = base::DictValue()
                             .Set("kind", "run")
                             .Set("time", base::TimeToValue(time))
                             .Set("steps", base::ListValue());
  if (!price) {
    return std::move(line)
        .Set("end", "failed")
        .Set("text", "fully.test didn't load")
        .Set("counted", true);
  }
  return std::move(line)
      .Set("end", "reported")
      .Set("found", found)
      .Set("what", found ? "Change alert" : "")
      .Set("text", "Price checked.")
      .Set("values", base::ListValue().Append(base::DictValue()
                                                  .Set("name", "price")
                                                  .Set("unit", "USD")
                                                  .Set("number", *price)));
}

class ScheduledPageBrowserTest : public headless::HeadlessModeBrowserTest {
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
    // The agent's model, which a new card starts on and runs use.
    Provider provider = PresetProvider(ProviderPreset::kCustom);
    provider.name = "Test server";
    provider.address = embedded_test_server()->GetURL("/v1").spec();
    provider_ = AddProvider(*browser()->GetProfile()->GetPrefs(), provider);
    SetChoice(*browser()->GetProfile()->GetPrefs(), ModelSlot::kAgent,
              provider_, "test-agent");
  }

  void TearDownOnMainThread() override {
    notifications_.reset();
    actor::ActorKeyedService::Get(browser()->GetProfile())->ResetForTesting();
    HeadlessModeBrowserTest::TearDownOnMainThread();
  }

  GURL Url(std::string_view path) {
    return embedded_https_test_server().GetURL("fully.test", path);
  }
  ScheduledChats* chats() {
    return ScheduledChats::For(browser()->GetProfile());
  }

  // Runs the scripted model through one run that reports `price`.
  void ScriptARun(double price = 549, bool found = false) {
    model_.Script(
        {ScriptedModel::Calls(base::ListValue().Append(ScriptedModel::Call(
             "report",
             base::DictValue()
                 .Set("found", found)
                 .Set("message", found ? "It's below $500." : "Still $549.")
                 .Set("values",
                      base::ListValue().Append(base::DictValue()
                                                   .Set("name", "price")
                                                   .Set("unit", "USD")
                                                   .Set("number", price)))))),
         ScriptedModel::Answer("Reported.")});
  }

  // A scheduled chat as a card saves it, after its first run.
  std::string Start(ScheduledChat chat) {
    ScriptARun();
    chat.provider_id = provider_;
    chat.model = "test-agent";
    const std::string id = chats()->Watch(chat);
    EXPECT_TRUE(base::test::RunUntil([&] { return !chats()->Running(id); }));
    return id;
  }

  std::string WatchTheDesk() {
    ScheduledChat watch;
    watch.request = "Tell me when the price of the desk drops below $500.";
    watch.page = Url("/desk").spec();
    watch.watch_for = "Price drops below $500";
    return Start(watch);
  }

  // Replaces the runs of `chat_id` with `runs`, oldest first.
  void SetRuns(const std::string& chat_id, base::ListValue runs) {
    base::test::TestFuture<std::optional<base::DictValue>> loaded;
    ChatFile(browser()->GetProfile()->GetPath(), chat_id)
        .Load(loaded.GetCallback());
    std::optional<base::DictValue> saved = loaded.Take();
    ASSERT_TRUE(saved);
    base::ListValue log;
    log.Append(saved->FindList("log")->front().Clone());
    for (base::Value& run : runs) {
      log.Append(std::move(run));
    }
    saved->Set("log", std::move(log));
    ChatFile(browser()->GetProfile()->GetPath(), chat_id).Save(*saved);
  }

  // Opens uw://scheduled in a new tab and returns its page.
  content::WebContents* OpenThePage() {
    EXPECT_TRUE(ui_test_utils::NavigateToURLWithDisposition(
        browser(), GURL(kScheduledUrl),
        WindowOpenDisposition::NEW_FOREGROUND_TAB,
        ui_test_utils::BROWSER_TEST_WAIT_FOR_LOAD_STOP));
    content::WebContents* page =
        browser()->tab_strip_model()->GetActiveWebContents();
    EXPECT_TRUE(Shows(page, "h1"));
    return page;
  }

  bool Shows(content::WebContents* page, std::string_view selector) {
    return base::test::RunUntil([&] {
      return content::EvalJs(
                 page, content::JsReplace("!!document.querySelector($1)",
                                          selector))
          .ExtractBool();
    });
  }

  std::string Text(content::WebContents* page, std::string_view selector) {
    return content::EvalJs(
               page,
               content::JsReplace(
                   "document.querySelector($1)?.textContent ?? ''", selector))
        .ExtractString();
  }

  bool Says(content::WebContents* page,
            std::string_view selector,
            std::string_view text) {
    return base::test::RunUntil(
        [&] { return Text(page, selector).find(text) != std::string::npos; });
  }

  // Types `text` into a field of the page, as a person would.
  void Type(content::WebContents* page,
            std::string_view selector,
            std::string_view text) {
    ASSERT_TRUE(content::ExecJs(
        page, content::JsReplace(
                  "const field = document.querySelector($1);"
                  "Object.getOwnPropertyDescriptor("
                  "Object.getPrototypeOf(field), 'value').set.call(field, $2);"
                  "field.dispatchEvent(new Event('input', {bubbles: true}));",
                  selector, text)));
  }

  void Click(content::WebContents* page, std::string_view selector) {
    ASSERT_TRUE(content::ExecJs(
        page,
        content::JsReplace("document.querySelector($1).click()", selector)));
  }

  ScopedMemoryKeychain keychain_;
  ScriptedModel model_{*embedded_test_server()};
  std::unique_ptr<NotificationDisplayServiceTester> notifications_;
  std::string provider_;
};

IN_PROC_BROWSER_TEST_F(ScheduledPageBrowserTest, WatchesAPageByItsAddress) {
  // A visit gives the page its title in history.
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), Url("/desk")));
  content::WebContents* page = OpenThePage();
  ASSERT_TRUE(Shows(page, "#scheduled-empty"));
  SaveWindowScreenshot(browser(), "scheduled-empty");

  Click(page, "#scheduled-empty #watch-a-page");
  ASSERT_TRUE(Shows(page, "#watch-address"));
  // Not a web address: the field says so once it is left.
  Type(page, "#watch-address", "jarvis desk price");
  ASSERT_TRUE(
      content::ExecJs(page,
                      "document.querySelector('#watch-address').dispatchEvent("
                      "new FocusEvent('focusout', {bubbles: true}))"));
  EXPECT_TRUE(Says(page, "[role=dialog]", "Enter a web address"));
  EXPECT_TRUE(content::EvalJs(page,
                              "document.querySelector('#watch-save')"
                              ".disabled")
                  .ExtractBool());

  // The address without https://, which uw adds.
  Type(page, "#watch-address",
       Url("/desk?utm_source=mail")
           .spec()
           .substr(std::string_view("https://").size()));
  ASSERT_TRUE(Says(page, "#watch-page", "Jarvis Bamboo Standing Desk"));
  SaveWindowScreenshot(browser(), "scheduled-watch-a-page");
  Click(page, "#watch-for-price");
  Type(page, "#watch-below", "$500");
  ScriptARun();
  Click(page, "#watch-save");

  ASSERT_TRUE(base::test::RunUntil(
      [&] { return chats()->ChatWatching(Url("/desk")) != nullptr; }));
  const std::string id = *chats()->ChatWatching(Url("/desk"));
  const ScheduledChat& watch = *chats()->Find(id);
  EXPECT_EQ(watch.page, Url("/desk").spec());
  EXPECT_EQ(watch.watch_for, "Price drops below $500");
  EXPECT_EQ(watch.request,
            "Tell me when the price of Jarvis Bamboo Standing Desk on "
            "fully.test drops below $500.");
  EXPECT_TRUE(base::test::RunUntil([&] { return !chats()->Running(id); }));
  ASSERT_TRUE(
      Shows(page, base::StrCat({"#watched-pages [data-row='", id, "']"})));
  EXPECT_TRUE(Says(page, "#watched-pages", "Jarvis Bamboo Standing Desk"));
  EXPECT_TRUE(Says(page, "#watched-pages", "$549"));

  // The same page again is already watched, and its watch opens in place.
  Click(page, "#watch-a-page");
  ASSERT_TRUE(Shows(page, "#watch-address"));
  Type(page, "#watch-address", Url("/desk#reviews").spec());
  ASSERT_TRUE(Shows(page, "#watch-already"));
  EXPECT_TRUE(content::EvalJs(page,
                              "document.querySelector('#watch-save')"
                              ".disabled")
                  .ExtractBool());
  Click(page, "#watch-open-watch");
  ASSERT_TRUE(Shows(page, "#watch-stop"));
  EXPECT_EQ(Text(page, "#watch-for"), "Price drops below $500");
  Click(page, "#watch-stop");
  ASSERT_TRUE(base::test::RunUntil([&] { return !chats()->Find(id); }));
  EXPECT_TRUE(Shows(page, "#scheduled-empty"));
}

IN_PROC_BROWSER_TEST_F(ScheduledPageBrowserTest, StartsAScheduledChat) {
  content::WebContents* page = OpenThePage();
  ASSERT_TRUE(Shows(page, "#scheduled-empty"));
  Click(page, "#scheduled-empty #new-scheduled-chat");
  ASSERT_TRUE(Shows(page, "#schedule-request"));
  EXPECT_TRUE(content::EvalJs(page,
                              "document.querySelector('#schedule-save')"
                              ".disabled")
                  .ExtractBool());
  Type(page, "#schedule-request", "Sum up the news about standing desks.");
  ScriptARun();
  Click(page, "#schedule-save");

  std::string id;
  ASSERT_TRUE(base::test::RunUntil([&] {
    for (const ChatIndex::Entry& entry :
         ChatIndex::For(browser()->GetProfile())->Entries()) {
      if (chats()->Find(entry.id)) {
        id = entry.id;
      }
    }
    return !id.empty();
  }));
  const ScheduledChat& chat = *chats()->Find(id);
  EXPECT_EQ(chat.request, "Sum up the news about standing desks.");
  EXPECT_TRUE(chat.page.empty());
  EXPECT_EQ(chat.model, "test-agent");
  EXPECT_TRUE(base::test::RunUntil([&] { return !chats()->Running(id); }));
  ASSERT_TRUE(
      Shows(page, base::StrCat({"#scheduled-chats [data-row='", id, "']"})));
  EXPECT_TRUE(
      Says(page, "#scheduled-chats", "Sum up the news about standing desks."));
  EXPECT_FALSE(
      content::EvalJs(page, "!!document.querySelector('#watched-pages')")
          .ExtractBool());
}

IN_PROC_BROWSER_TEST_F(ScheduledPageBrowserTest, RowsOpenTheirPageAndChat) {
  const std::string id = WatchTheDesk();
  content::WebContents* page = OpenThePage();
  TabStripModel* tabs = browser()->tab_strip_model();
  const int scheduled = tabs->active_index();
  const std::string row = base::StrCat({"[data-row='", id, "']"});
  ASSERT_TRUE(Shows(page, row));

  // The page opens in a new tab, and a second click goes back to that tab.
  const int count = tabs->count();
  Click(page, row);
  ASSERT_TRUE(base::test::RunUntil([&] {
    return tabs->GetActiveWebContents()->GetVisibleURL() == Url("/desk");
  }));
  EXPECT_EQ(tabs->count(), count + 1);
  const int desk = tabs->active_index();
  tabs->ActivateTabAt(scheduled);
  Click(page, row);
  ASSERT_TRUE(
      base::test::RunUntil([&] { return tabs->active_index() == desk; }));
  EXPECT_EQ(tabs->count(), count + 1);

  // Open chat opens the watch's chat.
  tabs->ActivateTabAt(scheduled);
  Click(page, base::StrCat({"[data-open-chat='", id, "']"}));
  EXPECT_TRUE(base::test::RunUntil([&] {
    return TabsShowingChat(browser()->GetProfile(), id).size() == 1;
  }));
}

IN_PROC_BROWSER_TEST_F(ScheduledPageBrowserTest, RowsShowHowTheirRunsWent) {
  const std::string desk = WatchTheDesk();
  ScheduledChat stock;
  stock.request = "Tell me when the desk is back in stock.";
  stock.page = Url("/desk?size=large").spec();
  stock.watch_for = "Back in stock";
  const std::string large = Start(stock);
  ScheduledChat briefing;
  briefing.request = "Three top stories on AI browsers.";
  briefing.regular_updates = true;
  const std::string news = Start(briefing);

  const base::Time now = base::Time::Now();
  base::ListValue desk_runs;
  desk_runs.Append(SavedRun(now - base::Days(2), 549));
  desk_runs.Append(SavedRun(now - base::Days(1), 549));
  desk_runs.Append(SavedRun(now - base::Hours(1), 479, /*found=*/true));
  SetRuns(desk, std::move(desk_runs));
  base::ListValue large_runs;
  large_runs.Append(SavedRun(now - base::Hours(2), std::nullopt));
  SetRuns(large, std::move(large_runs));
  chats()->SetPaused(news, true);

  content::WebContents* page = OpenThePage();
  const auto row = [](const std::string& id) {
    return base::StrCat({"[data-row='", id, "']"});
  };
  ASSERT_TRUE(Says(page, row(desk), "$479"));
  EXPECT_TRUE(Says(page, row(desk), "Down $70, was $549"));
  EXPECT_TRUE(Says(page, row(desk), "Change alert"));
  EXPECT_TRUE(Says(page, row(desk), "Price drops below $500"));
  EXPECT_TRUE(Says(page, row(large), "Didn't finish"));
  EXPECT_TRUE(Says(page, row(news), "Paused"));
  EXPECT_TRUE(Says(page, row(news), "By you"));
  EXPECT_TRUE(Says(page, row(news), "Regular updates"));
  EXPECT_TRUE(Says(page, "#scheduled-summary", "3 scheduled chats."));
  SaveWindowScreenshot(browser(), "scheduled-page");

  // Pause on a row, then Pause all and Resume all.
  Click(page, base::StrCat({"[data-pause='", desk, "']"}));
  ASSERT_TRUE(
      base::test::RunUntil([&] { return chats()->Find(desk)->paused; }));
  EXPECT_FALSE(chats()->Find(large)->paused);
  Click(page, "#pause-all");
  ASSERT_TRUE(
      base::test::RunUntil([&] { return chats()->Find(large)->paused; }));
  ASSERT_TRUE(Says(page, "#pause-all", "Resume all"));
  Click(page, "#pause-all");
  EXPECT_TRUE(base::test::RunUntil([&] {
    return !chats()->Find(desk)->paused && !chats()->Find(large)->paused &&
           !chats()->Find(news)->paused;
  }));
}

IN_PROC_BROWSER_TEST_F(ScheduledPageBrowserTest, TheCommandOpensThePage) {
  chrome::ExecuteCommand(browser(), kShowScheduled);
  EXPECT_TRUE(base::test::RunUntil([&] {
    return browser()
               ->tab_strip_model()
               ->GetActiveWebContents()
               ->GetVisibleURL() == GURL(kScheduledUrl);
  }));
}

}  // namespace
}  // namespace uw
