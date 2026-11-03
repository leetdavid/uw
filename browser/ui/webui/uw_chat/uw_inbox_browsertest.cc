// Copyright 2026 The uw Authors. All rights reserved.

#include <cmath>
#include <cstdlib>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "base/run_loop.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/test/run_until.h"
#include "base/test/scoped_run_loop_timeout.h"
#include "chrome/browser/actor/actor_keyed_service.h"
#include "chrome/browser/headless/test/headless_mode_browsertest.h"
#include "chrome/browser/notifications/notification_display_service_tester.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/tab_groups/tab_group_id.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "net/dns/mock_host_resolver.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "net/test/embedded_test_server/http_request.h"
#include "net/test/embedded_test_server/http_response.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/events/base_event_utils.h"
#include "ui/events/event.h"
#include "ui/events/event_constants.h"
#include "ui/gfx/geometry/point_f.h"
#include "ui/gfx/geometry/vector2d.h"
#include "ui/views/test/button_test_api.h"
#include "ui/views/widget/widget.h"
#include "uw/browser/agent/chat_alerts.h"
#include "uw/browser/agent/chat_tab.h"
#include "uw/browser/agent/scheduled_chats.h"
#include "uw/browser/agent/scripted_model.h"
#include "uw/browser/ai_providers/scoped_memory_keychain.h"
#include "uw/browser/tab_organization/tab_organization_controller.h"
#include "uw/browser/tab_organization/window_screenshot.h"
#include "uw/browser/ui/glass/glass.h"
#include "uw/browser/ui/views/tab_organization/inbox_button.h"
#include "uw/browser/ui/webui/uw_chat/uw_scheduled_ui.h"
#include "uw/browser/workspaces/workspace_theme.h"
#include "uw/components/ai_providers/providers.h"
#include "uw/components/look/theme.h"

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
    return WatchTheDeskAsScripted();
  }

  // Watches the desk, with a first run as the model is scripted, and waits
  // for that run's alert.
  std::string WatchTheDeskAsScripted() {
    ScheduledChat watch;
    watch.request = "Tell me when the price of the desk drops below $500.";
    watch.page = Desk().spec();
    watch.watch_for = "Price drops below $500";
    watch.provider_id = provider_;
    watch.model = "test-agent";
    const std::string id = chats()->Watch(watch);
    EXPECT_TRUE(base::test::RunUntil([&] { return !chats()->Running(id); }));
    EXPECT_TRUE(base::test::RunUntil([&] { return alerts()->Unread() == 1; }));
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

  TabOrganizationController* controller() {
    return TabOrganizationController::From(browser());
  }

  // Puts the active tab in a task of its own, beside a tab in the Scratchpad,
  // and returns the task.
  std::optional<tab_groups::TabGroupId> MakeATask() {
    chrome::AddTabAt(browser(), GURL("about:blank"), -1, false);
    TabStripModel* tabs = browser()->tab_strip_model();
    const std::optional<tab_groups::TabGroupId> task =
        controller()->MoveToNewTask(
            {controller()->IdFor(tabs->GetActiveTab()).value()}, u"Plan");
    base::RunLoop().RunUntilIdle();
    EXPECT_EQ(tabs->GetActiveTab()->GetGroup(), task);
    return task;
  }

  // Whether the active tab sits at the top of the Scratchpad, right after the
  // task `task`, the only one.
  bool ActiveTabTopsTheScratchpad(std::optional<tab_groups::TabGroupId> task) {
    TabStripModel* tabs = browser()->tab_strip_model();
    return !tabs->GetActiveTab()->GetGroup() && tabs->active_index() > 0 &&
           tabs->GetTabAtIndex(tabs->active_index() - 1)->GetGroup() == task;
  }

  GURL ActiveUrl() {
    return browser()
        ->tab_strip_model()
        ->GetActiveWebContents()
        ->GetLastCommittedURL();
  }

  // How tall the Inbox's content is, and how tall the pop-up shows its page.
  std::string Heights(content::WebContents* page) {
    return base::StrCat(
        {"content ",
         base::NumberToString(
             content::EvalJs(page,
                             "Math.ceil(document.querySelector('#inbox')"
                             ".getBoundingClientRect().height)")
                 .ExtractInt()),
         ", page viewport ",
         base::NumberToString(
             content::EvalJs(page, "innerHeight").ExtractInt()),
         ", pop-up ",
         base::NumberToString(page->GetContainerBounds().height())});
  }

  // Whether the pop-up shows `page` at its content's height.
  bool FitsItsContent(content::WebContents* page) {
    const int content =
        content::EvalJs(page,
                        "Math.ceil(document.querySelector('#inbox')"
                        ".getBoundingClientRect().height)")
            .ExtractInt();
    return std::abs(page->GetContainerBounds().height() - content) <= 1;
  }

  // The color of `window` at `point` in the Inbox button, averaged over the
  // pixels around it, so the tint's grain evens out.
  SkColor ButtonColorAt(const SkBitmap& window, gfx::PointF point) {
    views::Widget* widget =
        BrowserView::GetBrowserViewForBrowser(browser())->GetWidget();
    const float scale = static_cast<float>(window.width()) /
                        widget->GetWindowBoundsInScreen().width();
    const gfx::Vector2d offset =
        button()->GetBoundsInScreen().origin() -
        widget->GetWindowBoundsInScreen().origin();
    const int x = std::lround((point.x() + offset.x()) * scale);
    const int y = std::lround((point.y() + offset.y()) * scale);
    int r = 0, g = 0, b = 0;
    for (int dx = -1; dx <= 1; ++dx) {
      for (int dy = -1; dy <= 1; ++dy) {
        const SkColor pixel = window.getColor(x + dx, y + dy);
        r += SkColorGetR(pixel);
        g += SkColorGetG(pixel);
        b += SkColorGetB(pixel);
      }
    }
    return SkColorSetRGB(r / 9, g / 9, b / 9);
  }

  ScopedMemoryKeychain keychain_;
  ScriptedModel model_{*embedded_test_server()};
  std::unique_ptr<NotificationDisplayServiceTester> notifications_;
  std::string provider_;
};

IN_PROC_BROWSER_TEST_F(InboxBrowserTest, CountsUnreadAlertsUntilMarkedRead) {
  ASSERT_TRUE(button());
  EXPECT_EQ(button()->GetTooltipText(), u"Inbox");
  WatchTheDesk();
  EXPECT_TRUE(base::test::RunUntil(
      [&] { return button()->GetTooltipText() == u"Inbox, 1 unread"; }));
  SaveWindowScreenshot(browser(), "inbox-button-unread");

  // Opening the Inbox leaves the alert unread.
  content::WebContents* page = OpenTheInbox();
  ASSERT_TRUE(Shows(page, "[data-alert][data-unread]"));
  EXPECT_EQ(alerts()->Unread(), 1u);
  EXPECT_EQ(button()->GetTooltipText(), u"Inbox, 1 unread");
  EXPECT_NE(Text(page, "[data-alert]").find("Change alert"), std::string::npos);
  EXPECT_NE(Text(page, "[data-alert]").find("The desk is $479"),
            std::string::npos);
  EXPECT_NE(Text(page, "#inbox").find("1 scheduled chat"), std::string::npos);
  SaveWindowScreenshot(browser(), "inbox-button-open");
  SaveWidgetScreenshot(
      views::Widget::GetWidgetForNativeView(page->GetNativeView()), "inbox");

  Click(page, "#inbox-mark-read");
  EXPECT_TRUE(base::test::RunUntil([&] { return alerts()->Unread() == 0; }));
  EXPECT_EQ(button()->GetTooltipText(), u"Inbox");
  ASSERT_TRUE(Shows(page, "[data-alert]:not([data-unread])"));
  EXPECT_TRUE(content::EvalJs(
                  page, "document.querySelector('#inbox-mark-read').disabled")
                  .ExtractBool());

  // An alert that comes while the Inbox is open shows in it, unread.
  alerts()->Add(alerts()->alerts().front().chat_id, "No change", "Still $479.",
                /*waiting=*/false);
  EXPECT_TRUE(base::test::RunUntil([&] {
    return Text(page, "#inbox").find("Still $479.") != std::string::npos;
  }));
  EXPECT_EQ(alerts()->Unread(), 1u);
  ASSERT_TRUE(Shows(page, "[data-alert][data-unread]"));
}

IN_PROC_BROWSER_TEST_F(InboxBrowserTest, AFailedRunsAlertOffersRetry) {
  model_.Script({ScriptedModel::Answer("I looked around.")});
  const std::string id = WatchTheDeskAsScripted();
  content::WebContents* page = OpenTheInbox();
  ASSERT_TRUE(Shows(page, "[data-retry]"));
  EXPECT_NE(Text(page, "[data-alert]").find("Couldn't check"),
            std::string::npos);
  SaveWidgetScreenshot(
      views::Widget::GetWidgetForNativeView(page->GetNativeView()),
      "inbox-retry");

  // Retry runs the chat out of sight and marks the alert read; the Inbox
  // stays open and shows the run going.
  model_.Script({ScriptedModel::Hang()});
  const size_t asked = model_.Requests().size();
  Click(page, "[data-retry]");
  EXPECT_TRUE(base::test::RunUntil(
      [&] { return model_.Requests().size() == asked + 1; }));
  EXPECT_TRUE(base::test::RunUntil([&] {
    return Text(page, "[data-alert]").find("Running now") != std::string::npos;
  }));
  EXPECT_FALSE(content::EvalJs(page, "!!document.querySelector('[data-retry]')")
                   .ExtractBool());
  EXPECT_TRUE(alerts()->alerts()[0].read);
  EXPECT_TRUE(button()->IsShowing());
  SaveWidgetScreenshot(
      views::Widget::GetWidgetForNativeView(page->GetNativeView()),
      "inbox-retry-running");

  // A stopped run leaves the chat failing, so Retry comes back. A run that
  // finds nothing new still sends its alert, and Retry goes.
  chats()->Stop(id);
  ASSERT_TRUE(Shows(page, "[data-retry]"));
  model_.Script(
      {ScriptedModel::Calls(base::ListValue().Append(
           ScriptedModel::Call("report", base::DictValue()
                                             .Set("found", false)
                                             .Set("message", "Still $549.")))),
       ScriptedModel::Answer("Reported.")});
  Click(page, "[data-retry]");
  EXPECT_TRUE(base::test::RunUntil([&] {
    return Text(page, "#inbox").find("Still $549.") != std::string::npos;
  }));
  ASSERT_EQ(alerts()->alerts().size(), 2u);
  EXPECT_EQ(alerts()->alerts()[0].what, "No change");
  EXPECT_FALSE(content::EvalJs(page, "!!document.querySelector('[data-retry]')")
                   .ExtractBool());
}

IN_PROC_BROWSER_TEST_F(InboxBrowserTest, AnAlertOpensItsPageOrItsChat) {
  const std::string id = WatchTheDesk();
  const std::optional<tab_groups::TabGroupId> task = MakeATask();
  ASSERT_TRUE(task);

  // A watched page's alert opens the page at the top of the Scratchpad, and
  // is read.
  content::WebContents* page = OpenTheInbox();
  ASSERT_TRUE(Shows(page, "[data-alert]"));
  Click(page, "[data-alert]");
  EXPECT_TRUE(base::test::RunUntil([&] { return !button()->IsShowing(); }));
  EXPECT_TRUE(base::test::RunUntil([&] { return ActiveUrl() == Desk(); }));
  EXPECT_TRUE(ActiveTabTopsTheScratchpad(task));
  EXPECT_EQ(alerts()->Unread(), 0u);

  // Its Open chat chip opens the chat there too.
  browser()->tab_strip_model()->ActivateTabAt(0);
  page = OpenTheInbox();
  ASSERT_TRUE(Shows(page, "[data-open-chat]"));
  Click(page, "[data-open-chat]");
  EXPECT_TRUE(base::test::RunUntil([&] { return ShowsChat(id); }));
  EXPECT_TRUE(ActiveTabTopsTheScratchpad(task));

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

// Real windows only: a headless pop-up never takes its page's size.
IN_PROC_BROWSER_TEST_F(InboxBrowserTest, MANUAL_ClearAllShrinksThePopUp) {
  const std::string id = WatchTheDesk();
  for (int i = 0; i < 5; ++i) {
    alerts()->Add(id, "No change", "Still $479.", /*waiting=*/false);
  }
  content::WebContents* page = OpenTheInbox();
  ASSERT_TRUE(Shows(page, "[data-alert]"));
  EXPECT_TRUE(base::test::RunUntil([&] { return FitsItsContent(page); }))
      << Heights(page);
  const int full = page->GetContainerBounds().height();
  views::Widget* popup =
      views::Widget::GetWidgetForNativeView(page->GetNativeView());
  SaveWidgetScreenshot(popup, "inbox-full");

  Click(page, "#inbox-clear");
  ASSERT_TRUE(Shows(page, "#inbox-empty"));
  {
    base::test::ScopedRunLoopTimeout timeout(FROM_HERE, base::Seconds(10));
    EXPECT_TRUE(base::test::RunUntil([&] { return FitsItsContent(page); }))
        << Heights(page);
  }
  EXPECT_LT(page->GetContainerBounds().height(), full);
  SaveWidgetScreenshot(popup, "inbox-shrunk");
}

// Real windows only: headless windows draw no glass. Over glass, the ring
// around the badge shows what is behind the button, the same as the button's
// corner, which neither the fill nor the icon reaches.
IN_PROC_BROWSER_TEST_F(InboxBrowserTest, MANUAL_TheBadgesRingShowsTheSidebar) {
  if (!CanShowGlass() || ReducesTransparency()) {
    GTEST_SKIP() << "This Mac draws no glass.";
  }
  browser()->GetWindow()->Activate();
  Theme theme = ThemeOfWindow(browser());
  theme.transparency = kFullTransparency / 2;
  SetThemeOfWindow(browser(), theme);
  ASSERT_TRUE(base::test::RunUntil([&] { return ShowsGlass(browser()); }));
  WatchTheDesk();
  for (const bool open : {false, true}) {
    if (open) {
      OpenTheInbox();
    }
    CaptureWindow(browser());
    const SkBitmap window = CaptureNativeWindow(browser());
    SaveBitmap(window, open ? "inbox-ring-open" : "inbox-ring");
    // Left of the one-figure badge, halfway across its ring.
    const SkColor ring = ButtonColorAt(window, gfx::PointF(12, 7.5));
    const SkColor corner = ButtonColorAt(window, gfx::PointF(1, 1));
    for (const int shift : {16, 8, 0}) {
      EXPECT_NEAR((ring >> shift) & 0xFF, (corner >> shift) & 0xFF, 8)
          << (open ? "open: " : "closed: ") << std::hex << ring << " vs "
          << corner;
    }
  }
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
