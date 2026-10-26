// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/browser/ui/views/watch_card/watch_card.h"

#include <array>
#include <memory>
#include <string>
#include <string_view>

#include "base/json/values_util.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/test/run_until.h"
#include "base/test/test_future.h"
#include "chrome/browser/actor/actor_keyed_service.h"
#include "chrome/browser/headless/test/headless_mode_browsertest.h"
#include "chrome/browser/notifications/notification_display_service_tester.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_actions.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/page_action/action_ids.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/location_bar/location_bar_view.h"
#include "chrome/browser/ui/views/page_action/page_action_container_view.h"
#include "chrome/browser/ui/views/page_action/page_action_view.h"
#include "chrome/browser/ui/views/toolbar/toolbar_view.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/prefs/pref_service.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "content/public/test/test_navigation_observer.h"
#include "net/dns/mock_host_resolver.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "net/test/embedded_test_server/http_request.h"
#include "net/test/embedded_test_server/http_response.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "ui/actions/actions.h"
#include "uw/browser/agent/chat_file.h"
#include "uw/browser/agent/chat_index.h"
#include "uw/browser/agent/chat_tab.h"
#include "uw/browser/agent/scheduled_chats.h"
#include "uw/browser/agent/scripted_model.h"
#include "uw/browser/ai_providers/scoped_memory_keychain.h"
#include "uw/browser/tab_organization/commands.h"
#include "uw/browser/tab_organization/window_screenshot.h"
#include "uw/browser/ui/webui/uw_chat/uw_watch_card_handler.h"
#include "uw/components/ai_providers/providers.h"

namespace uw {
namespace {

constexpr char kDesk[] = R"(<!doctype html>
<title>Jarvis Bamboo Standing Desk</title>
<p>Price: $549</p>)";

// A scheduled run's line as a chat file keeps it: a run that reported
// `price` and `in_stock`, or one that didn't finish.
base::DictValue SavedRun(base::Time time,
                         std::optional<double> price,
                         bool in_stock = false) {
  base::DictValue line = base::DictValue()
                             .Set("kind", "run")
                             .Set("time", base::TimeToValue(time))
                             .Set("steps", base::ListValue());
  if (!price) {
    return std::move(line)
        .Set("end", "failed")
        .Set("text", "fully.com didn't load")
        .Set("counted", true);
  }
  return std::move(line)
      .Set("end", "reported")
      .Set("what", "Update")
      .Set("text", base::StrCat({"It's $", base::NumberToString(*price), "."}))
      .Set("values", base::ListValue()
                         .Append(base::DictValue()
                                     .Set("name", "price")
                                     .Set("unit", "USD")
                                     .Set("number", *price))
                         .Append(base::DictValue()
                                     .Set("name", "in_stock")
                                     .Set("unit", "")
                                     .Set("yes", in_stock)));
}

constexpr char16_t kWatch[] = u"Watch this page";
constexpr char16_t kWatching[] = u"Watching for price drops below $500";

class WatchCardBrowserTest : public headless::HeadlessModeBrowserTest {
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
    // The agent's model, which the card starts on and runs use.
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
  WatchCard* card() { return WatchCard::From(browser()); }

  // What the eye says, or nothing while it is hidden.
  std::u16string Eye() {
    page_actions::PageActionView* eye =
        BrowserView::GetBrowserViewForBrowser(browser())
            ->toolbar()
            ->location_bar_view()
            ->page_action_container()
            ->GetPageActionView(page_actions::kActionUwWatchPage);
    return eye && eye->GetVisible() ? eye->GetTooltipText() : u"";
  }

  bool EyeSays(std::u16string_view text) {
    return base::test::RunUntil([&] { return Eye() == text; });
  }

  // Runs the scripted model through one run that reports.
  void ScriptARun() {
    model_.Script(
        {ScriptedModel::Calls(base::ListValue().Append(ScriptedModel::Call(
             "report", base::DictValue()
                           .Set("found", false)
                           .Set("message", "Still $549.")
                           .Set("values", base::ListValue().Append(
                                              base::DictValue()
                                                  .Set("name", "price")
                                                  .Set("unit", "USD")
                                                  .Set("number", 549)))))),
         ScriptedModel::Answer("Reported.")});
  }

  // A watch of the desk, as the card saves it.
  std::string WatchTheDesk() {
    ScriptARun();
    ScheduledChat watch;
    watch.request =
        "Tell me when the price of Jarvis Bamboo Standing Desk on fully.test "
        "drops below $500.";
    watch.page = Url("/desk").spec();
    watch.watch_for = "Price drops below $500";
    watch.provider_id = provider_;
    watch.model = "test-agent";
    const std::string id = chats()->Watch(watch);
    EXPECT_TRUE(base::test::RunUntil([&] { return !chats()->Running(id); }));
    return id;
  }

  // Waits for the card's page to show `selector`.
  bool Shows(content::WebContents* page, std::string_view selector) {
    return base::test::RunUntil([&] {
      return content::EvalJs(page, base::StrCat({"!!document.querySelector('",
                                                 selector, "')"}))
          .ExtractBool();
    });
  }

  // Types `text` into a field of the card's page, as a person would.
  void Type(content::WebContents* page,
            std::string_view selector,
            std::string_view text) {
    ASSERT_TRUE(content::ExecJs(
        page, content::JsReplace(
                  "const field = document.querySelector($1);"
                  "Object.getOwnPropertyDescriptor(HTMLInputElement.prototype, "
                  "'value').set.call(field, $2);"
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

IN_PROC_BROWSER_TEST_F(WatchCardBrowserTest,
                       TheEyeShowsOnWebPagesAndFillsOnAWatchedPage) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), Url("/desk")));
  ASSERT_TRUE(EyeSays(kWatch));

  WatchTheDesk();
  EXPECT_TRUE(EyeSays(kWatching));

  // The same page, with tracking parameters and a part after #, is watched.
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), Url("/desk?utm_source=newsletter&ref=home#reviews")));
  EXPECT_TRUE(EyeSays(kWatching));
  // Another page is not.
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), Url("/desk?size=large")));
  EXPECT_TRUE(EyeSays(kWatch));
  // Nor are uw's own pages, which have no eye.
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), ChatUrl()));
  EXPECT_TRUE(EyeSays(u""));
}

IN_PROC_BROWSER_TEST_F(WatchCardBrowserTest, TheCardWatchesThePage) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), Url("/desk")));
  ASSERT_TRUE(EyeSays(kWatch));

  // A click on the eye runs its action.
  actions::ActionItem* eye = actions::ActionManager::Get().FindAction(
      page_actions::kActionUwWatchPage,
      BrowserActions::From(browser())->root_action_item());
  ASSERT_TRUE(eye);
  eye->InvokeAction();
  ASSERT_TRUE(card()->page());
  content::WebContents* page = card()->page();
  ASSERT_TRUE(Shows(page, "#watch-for-price"));
  EXPECT_EQ(content::EvalJs(page,
                            "document.querySelector('#watch-page')"
                            ".textContent"),
            "Jarvis Bamboo Standing Desk · fully.test");
  EXPECT_TRUE(base::test::RunUntil([&] { return card()->IsShowing(); }));

  ScriptARun();
  Click(page, "#watch-for-price");
  Type(page, "#watch-below", "$500");
  Click(page, "#watch-save");

  ASSERT_TRUE(base::test::RunUntil(
      [&] { return chats()->ChatWatching(Url("/desk")) != nullptr; }));
  const std::string id = *chats()->ChatWatching(Url("/desk"));
  const ScheduledChat& watch = *chats()->Find(id);
  EXPECT_EQ(watch.request,
            "Tell me when the price of Jarvis Bamboo Standing Desk on "
            "fully.test drops below $500.");
  EXPECT_EQ(watch.watch_for, "Price drops below $500");
  EXPECT_EQ(watch.page, Url("/desk").spec());
  EXPECT_EQ(watch.model, "test-agent");
  EXPECT_TRUE(base::test::RunUntil([&] { return !card()->IsShowing(); }));
  EXPECT_TRUE(EyeSays(kWatching));
  EXPECT_TRUE(base::test::RunUntil([&] { return !chats()->Running(id); }));
}

IN_PROC_BROWSER_TEST_F(WatchCardBrowserTest,
                       AWatchedPagesCardOpensItsChatAndStopsWatching) {
  const std::string id = WatchTheDesk();
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), Url("/desk")));
  ASSERT_TRUE(EyeSays(kWatching));

  // The command does what a click on the eye does.
  chrome::ExecuteCommand(browser(), kWatchThisPage);
  content::WebContents* page = card()->page();
  ASSERT_TRUE(page);
  ASSERT_TRUE(Shows(page, "#watch-open-chat"));
  EXPECT_EQ(content::EvalJs(page,
                            "document.querySelector('#watch-for')"
                            ".textContent"),
            "Price drops below $500");
  Click(page, "#watch-open-chat");
  ASSERT_TRUE(base::test::RunUntil([&] {
    return TabsShowingChat(browser()->GetProfile(), id).size() == 1;
  }));
  EXPECT_TRUE(base::test::RunUntil([&] { return !card()->IsShowing(); }));

  browser()->tab_strip_model()->ActivateTabAt(0);
  ASSERT_TRUE(WatchCard::Open(browser()));
  page = card()->page();
  ASSERT_TRUE(Shows(page, "#watch-stop"));
  Click(page, "#watch-stop");
  ASSERT_TRUE(base::test::RunUntil([&] { return !chats()->Find(id); }));
  EXPECT_TRUE(EyeSays(kWatch));
  // The chat stays.
  bool kept = false;
  for (const ChatIndex::Entry& entry :
       ChatIndex::For(browser()->GetProfile())->Entries()) {
    kept = kept || entry.id == id;
  }
  EXPECT_TRUE(kept);
}

// A headless bubble never draws its page, so the card is shown in a tab to
// check it against the frames "Watch this page: the card" and "Watch this
// page: every state".
IN_PROC_BROWSER_TEST_F(WatchCardBrowserTest, TheCardLooksLikeTheFrames) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), Url("/desk")));
  tabs::TabInterface* desk = browser()->GetActiveTabInterface();
  ASSERT_TRUE(ui_test_utils::NavigateToURLWithDisposition(
      browser(), WatchCardUrl(*desk), WindowOpenDisposition::NEW_FOREGROUND_TAB,
      ui_test_utils::BROWSER_TEST_WAIT_FOR_LOAD_STOP));
  content::WebContents* page =
      browser()->tab_strip_model()->GetActiveWebContents();
  ASSERT_TRUE(Shows(page, "#watch-for-price"));
  Click(page, "#watch-for-price");
  Type(page, "#watch-below", "$500");
  SaveWindowScreenshot(browser(), "watch-card");

  ScriptARun();
  Click(page, "#watch-save");
  ASSERT_TRUE(Shows(page, "#watch-open-chat"));
  const std::string id = *chats()->ChatWatching(Url("/desk"));
  EXPECT_TRUE(base::test::RunUntil([&] { return !chats()->Running(id); }));
  ASSERT_TRUE(base::test::RunUntil([&] {
    return content::EvalJs(page,
                           "document.querySelector('#watch-last')"
                           "?.textContent ?? ''")
               .ExtractString()
               .find("Still $549.") != std::string::npos;
  }));

  // Earlier runs, as if the page had been watched for a week, with one that
  // didn't finish.
  base::test::TestFuture<std::optional<base::DictValue>> loaded;
  ChatFile(browser()->GetProfile()->GetPath(), id).Load(loaded.GetCallback());
  std::optional<base::DictValue> saved = loaded.Take();
  ASSERT_TRUE(saved);
  const base::ListValue& kept = *saved->FindList("log");
  base::ListValue log;
  log.Append(kept.front().Clone());
  const auto prices = std::to_array<std::optional<double>>(
      {599, 589, std::nullopt, 579, 569, 559});
  for (size_t day = 0; day < std::size(prices); ++day) {
    log.Append(SavedRun(base::Time::Now() - base::Days(6 - day), prices[day]));
  }
  for (size_t i = 1; i < kept.size(); ++i) {
    log.Append(kept[i].Clone());
  }
  saved->Set("log", std::move(log));
  ChatFile(browser()->GetProfile()->GetPath(), id).Save(*saved);
  content::TestNavigationObserver reloaded(page);
  page->GetController().Reload(content::ReloadType::NORMAL, false);
  reloaded.Wait();
  ASSERT_TRUE(Shows(page, "#watch-chart svg line"));
  EXPECT_THAT(content::EvalJs(page,
                              "document.querySelector('#watch-chart p')"
                              ".textContent")
                  .ExtractString(),
              testing::StartsWith("$549Down $50 since "));
  SaveWindowScreenshot(browser(), "watch-card-watched");
}

}  // namespace
}  // namespace uw
