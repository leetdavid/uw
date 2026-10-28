// Copyright 2026 The uw Authors. All rights reserved.

#include <memory>
#include <string>
#include <string_view>

#include "base/strings/strcat.h"
#include "base/test/run_until.h"
#include "chrome/browser/actor/actor_keyed_service.h"
#include "chrome/browser/headless/test/headless_mode_browsertest.h"
#include "chrome/browser/notifications/notification_display_service_tester.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "net/dns/mock_host_resolver.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "net/test/embedded_test_server/http_request.h"
#include "net/test/embedded_test_server/http_response.h"
#include "ui/events/base_event_utils.h"
#include "ui/events/event.h"
#include "ui/events/event_constants.h"
#include "ui/views/test/button_test_api.h"
#include "ui/views/widget/widget.h"
#include "uw/browser/agent/chat_alerts.h"
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

class InboxBrowserTest : public headless::HeadlessModeBrowserTest {
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

  GURL Desk() { return embedded_https_test_server().GetURL("fully.test", "/desk"); }
  ScheduledChats* chats() {
    return ScheduledChats::For(browser()->GetProfile());
  }
  ChatAlerts* alerts() { return ChatAlerts::For(browser()->GetProfile()); }
  InboxButton* button() {
    return FindInboxButton(
        BrowserView::GetBrowserViewForBrowser(browser())->GetWidget()
            ->GetRootView());
  }

  // Watches the desk with a first run that finds its price below $500, which
  // notifies, so the Inbox keeps a Change alert.
  std::string WatchTheDesk() {
    model_.Script(
        {ScriptedModel::Calls(base::ListValue().Append(ScriptedModel::Call(
             "report", base::DictValue()
                           .Set("found", true)
                           .Set("message", "The desk is $479, below your $500.")
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
    watch.provider_id = provider_;
    watch.model = "test-agent";
    const std::string id = chats()->Watch(watch);
    EXPECT_TRUE(base::test::RunUntil([&] { return !chats()->Running(id); }));
    EXPECT_TRUE(base::test::RunUntil([&] { return alerts()->Unseen() == 1; }));
    return id;
  }

  // Presses the Inbox button and returns the Inbox's page once it shows.
  content::WebContents* OpenTheInbox() {
    views::test::ButtonTestApi(button()).NotifyClick(
        ui::MouseEvent(ui::EventType::kMousePressed, gfx::Point(), gfx::Point(),
                       ui::EventTimeForNow(), ui::EF_LEFT_MOUSE_BUTTON, 0));
    EXPECT_TRUE(base::test::RunUntil([&] { return button()->IsShowing(); }));
    content::WebContents* page = button()->page();
    EXPECT_TRUE(Shows(page, "#inbox"));
    return page;
  }

  bool Shows(content::WebContents* page, std::string_view selector) {
    return base::test::RunUntil([&] {
      return content::EvalJs(
                 page,
                 content::JsReplace("!!document.querySelector($1)", selector))
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

  void Click(content::WebContents* page, std::string_view selector) {
    ASSERT_TRUE(content::ExecJs(
        page,
        content::JsReplace("document.querySelector($1).click()", selector)));
  }

  // Whether the active tab shows the chat `id`.
  bool ShowsChat(const std::string& id) {
    for (tabs::TabInterface* tab :
         TabsShowingChat(browser()->GetProfile(), id)) {
      if (tab->IsActivated()) {
        return true;
      }
    }
    return false;
  }

  GURL ActiveUrl() {
    return browser()
        ->tab_strip_model()
        ->GetActiveWebContents()
        ->GetLastCommittedURL();
  }

  ScopedMemoryKeychain keychain_;
  ScriptedModel model_{*embedded_test_server()};
  std::unique_ptr<NotificationDisplayServiceTester> notifications_;
  std::string provider_;
};

IN_PROC_BROWSER_TEST_F(InboxBrowserTest, CountsUnseenAlertsUntilOpened) {
  ASSERT_TRUE(button());
  EXPECT_EQ(button()->GetTooltipText(), u"Inbox");
  WatchTheDesk();
  EXPECT_TRUE(base::test::RunUntil(
      [&] { return button()->GetTooltipText() == u"Inbox, 1 new"; }));
  SaveWindowScreenshot(browser(), "inbox-button-unseen");

  content::WebContents* page = OpenTheInbox();
  EXPECT_EQ(alerts()->Unseen(), 0u);
  EXPECT_EQ(button()->GetTooltipText(), u"Inbox");
  ASSERT_TRUE(Shows(page, "[data-alert]"));
  EXPECT_NE(Text(page, "[data-alert]").find("Change alert"), std::string::npos);
  EXPECT_NE(Text(page, "[data-alert]").find("The desk is $479"),
            std::string::npos);
  EXPECT_NE(Text(page, "#inbox").find("1 scheduled chat"), std::string::npos);
  SaveWindowScreenshot(browser(), "inbox-button-open");
  SaveWidgetScreenshot(
      views::Widget::GetWidgetForNativeView(page->GetNativeView()), "inbox");

  // An alert that comes while the Inbox is open shows in it.
  alerts()->Add(alerts()->alerts().front().chat_id, "No change", "Still $479.",
                /*waiting=*/false);
  EXPECT_TRUE(base::test::RunUntil([&] {
    return Text(page, "#inbox").find("Still $479.") != std::string::npos;
  }));
}

IN_PROC_BROWSER_TEST_F(InboxBrowserTest, AnAlertOpensItsPageOrItsChat) {
  const std::string id = WatchTheDesk();

  // A watched page's alert opens the page.
  content::WebContents* page = OpenTheInbox();
  ASSERT_TRUE(Shows(page, "[data-alert]"));
  Click(page, "[data-alert]");
  EXPECT_TRUE(base::test::RunUntil([&] { return !button()->IsShowing(); }));
  EXPECT_TRUE(base::test::RunUntil([&] { return ActiveUrl() == Desk(); }));

  // Its Open chat chip opens the chat.
  page = OpenTheInbox();
  ASSERT_TRUE(Shows(page, "[data-open-chat]"));
  Click(page, "[data-open-chat]");
  EXPECT_TRUE(base::test::RunUntil([&] { return ShowsChat(id); }));

  // An alert waiting for approval opens the chat, where the step is answered.
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), GURL("about:blank")));
  alerts()->Add(id, "Waits for your approval", "Open the desk's page",
                /*waiting=*/true);
  page = OpenTheInbox();
  const std::string waiting = alerts()->alerts().front().id;
  ASSERT_TRUE(Shows(page, base::StrCat({"[data-alert='", waiting, "']"})));
  EXPECT_FALSE(content::EvalJs(page, content::JsReplace(
                                         "!!document.querySelector($1)",
                                         base::StrCat({"[data-open-chat='",
                                                       waiting, "']"})))
                   .ExtractBool());
  Click(page, base::StrCat({"[data-alert='", waiting, "']"}));
  EXPECT_TRUE(base::test::RunUntil([&] { return ShowsChat(id); }));
}

IN_PROC_BROWSER_TEST_F(InboxBrowserTest, ClearAllEmptiesTheInbox) {
  WatchTheDesk();
  content::WebContents* page = OpenTheInbox();
  ASSERT_TRUE(Shows(page, "[data-alert]"));
  Click(page, "#inbox-clear");
  EXPECT_TRUE(base::test::RunUntil([&] { return alerts()->alerts().empty(); }));
  ASSERT_TRUE(Shows(page, "#inbox-empty"));
  EXPECT_TRUE(
      content::EvalJs(page, "document.querySelector('#inbox-clear').disabled")
          .ExtractBool());
  SaveWidgetScreenshot(
      views::Widget::GetWidgetForNativeView(page->GetNativeView()),
      "inbox-empty");

  // See all scheduled opens the Scheduled page.
  Click(page, "#inbox-see-all");
  EXPECT_TRUE(base::test::RunUntil(
      [&] { return ActiveUrl() == GURL(kScheduledUrl); }));
  EXPECT_TRUE(base::test::RunUntil([&] { return !button()->IsShowing(); }));
}

IN_PROC_BROWSER_TEST_F(InboxBrowserTest, NoButtonInAPrivateWindow) {
  EXPECT_TRUE(button());
  Browser* private_window = CreateIncognitoBrowser();
  EXPECT_FALSE(FindInboxButton(
      BrowserView::GetBrowserViewForBrowser(private_window)
          ->GetWidget()
          ->GetRootView()));
}

}  // namespace
}  // namespace uw
