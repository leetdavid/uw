// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/browser/agent/scheduled_chats.h"

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "base/environment.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/json/values_util.h"
#include "base/test/run_until.h"
#include "base/test/test_future.h"
#include "base/threading/thread_restrictions.h"
#include "chrome/browser/actor/actor_keyed_service.h"
#include "chrome/browser/headless/test/headless_mode_browsertest.h"
#include "chrome/browser/notifications/notification_display_service_tester.h"
#include "chrome/browser/profiles/keep_alive/profile_keep_alive_types.h"
#include "chrome/browser/profiles/keep_alive/scoped_profile_keep_alive.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "components/keep_alive_registry/keep_alive_types.h"
#include "components/keep_alive_registry/scoped_keep_alive.h"
#include "components/prefs/pref_service.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "net/dns/mock_host_resolver.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "net/test/embedded_test_server/http_request.h"
#include "net/test/embedded_test_server/http_response.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "ui/message_center/public/cpp/notification.h"
#include "uw/browser/agent/chat_alerts.h"
#include "uw/browser/agent/chat_file.h"
#include "uw/browser/agent/chat_index.h"
#include "uw/browser/agent/chat_tab.h"
#include "uw/browser/agent/chat_triggers.h"
#include "uw/browser/agent/scripted_model.h"
#include "uw/browser/ai_providers/scoped_memory_keychain.h"
#include "uw/components/ai_providers/providers.h"

namespace uw {
namespace {

using ::testing::HasSubstr;

constexpr char kChat[] = "2f1c7a9e-4b2d-4c8e-9a51-0d6e3b7f8c21";

constexpr char kDesk[] = R"(<!doctype html>
<title>Jarvis desk</title>
<p>Price: $479</p>
<button>Buy</button>)";

class ScheduledChatsBrowserTest : public headless::HeadlessModeBrowserTest {
 protected:
  void SetUpOnMainThread() override {
    HeadlessModeBrowserTest::SetUpOnMainThread();
    host_resolver()->AddRule("shop.test", "127.0.0.1");
    embedded_https_test_server().SetCertHostnames({"shop.test"});
    embedded_https_test_server().RegisterRequestHandler(base::BindRepeating(
        [](const net::test_server::HttpRequest& request)
            -> std::unique_ptr<net::test_server::HttpResponse> {
          if (request.relative_url != "/desk") {
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
    notifications_ =
        std::make_unique<NotificationDisplayServiceTester>(profile());
  }

  void TearDownOnMainThread() override {
    notifications_.reset();
    actor::ActorKeyedService::Get(profile())->ResetForTesting();
    HeadlessModeBrowserTest::TearDownOnMainThread();
  }

  Profile* profile() { return browser()->GetProfile(); }
  ScheduledChats* chats() { return ScheduledChats::For(profile()); }

  std::string Url(std::string_view path) {
    return embedded_https_test_server().GetURL("shop.test", path).spec();
  }

  // A provider at the scripted model, as the schedule card would pick it.
  std::string ProviderId() {
    Provider provider = PresetProvider(ProviderPreset::kCustom);
    provider.name = "Test server";
    provider.address = embedded_test_server()->GetURL("/v1").spec();
    return AddProvider(*profile()->GetPrefs(), provider);
  }

  // Saves the chat a schedule belongs to, as the chat page would.
  void SaveChat() {
    ChatIndex::For(profile())->Update({kChat, "Jarvis desk price",
                                       base::Time::Now(),
                                       ChatIndex::State::kDone});
    ChatFile(profile()->GetPath(), kChat)
        .Save(base::DictValue()
                  .Set("version", 1)
                  .Set("title", "Jarvis desk price")
                  .Set("log", base::ListValue()));
  }

  ScheduledChat DeskWatch(std::string provider_id) {
    ScheduledChat chat;
    chat.schedule.repeat = Schedule::Repeat::kDaily;
    chat.schedule.hour = 9;
    chat.request = "Tell me when the Jarvis desk drops below $500.";
    chat.provider_id = std::move(provider_id);
    chat.model = "test-agent";
    return chat;
  }

  // Waits until the chat's run ends, and returns the chat's saved lines.
  base::ListValue LinesAfterTheRun(std::string_view chat_id = kChat) {
    EXPECT_TRUE(base::test::RunUntil(
        [&] { return chats()->loaded() && !chats()->Running(chat_id); }));
    base::test::TestFuture<std::optional<base::DictValue>> loaded;
    ChatFile(profile()->GetPath(), chat_id).Load(loaded.GetCallback());
    std::optional<base::DictValue> chat = loaded.Take();
    const base::ListValue* log = chat ? chat->FindList("log") : nullptr;
    return log ? log->Clone() : base::ListValue();
  }

  std::vector<message_center::Notification> Notifications() {
    return notifications_->GetDisplayedNotificationsForType(
        NotificationHandler::Type::UW_CHAT);
  }

  static std::string Report(bool found, std::string_view message) {
    return ScriptedModel::Calls(base::ListValue().Append(ScriptedModel::Call(
        "report", base::DictValue()
                      .Set("found", found)
                      .Set("message", message)
                      .Set("values", base::ListValue().Append(
                                         base::DictValue()
                                             .Set("name", "price")
                                             .Set("unit", "USD")
                                             .Set("number", 479))))));
  }

  ScopedMemoryKeychain keychain_;
  ScriptedModel model_{*embedded_test_server()};
  std::unique_ptr<NotificationDisplayServiceTester> notifications_;
};

IN_PROC_BROWSER_TEST_F(ScheduledChatsBrowserTest,
                       ASavedScheduleRunsAtOnceAndTellsWhatItFound) {
  SaveChat();
  model_.Script(
      {ScriptedModel::Calls(base::ListValue().Append(ScriptedModel::Call(
           "open", base::DictValue().Set("url", Url("/desk"))))),
       ScriptedModel::WithUsage(
           Report(true, "The desk is $479, below your $500."), 900, 30),
       ScriptedModel::Answer("Reported.")});

  chats()->Save(kChat, DeskWatch(ProviderId()));
  const base::ListValue lines = LinesAfterTheRun();

  ASSERT_EQ(lines.size(), 1u);
  const base::DictValue& run = lines[0].GetDict();
  EXPECT_EQ(*run.FindString("kind"), "run");
  EXPECT_EQ(*run.FindString("end"), "reported");
  EXPECT_EQ(*run.FindString("text"), "The desk is $479, below your $500.");
  EXPECT_EQ(*run.FindString("what"), "Change alert");
  EXPECT_EQ(run.FindList("values")->front().GetDict().FindDouble("number"),
            479);
  EXPECT_EQ(run.FindList("steps")->size(), 2u);
  // What the run's requests used goes on its line; this model has no price.
  EXPECT_EQ(run.FindDoubleByDottedPath("usage.input"), 900);
  EXPECT_EQ(run.FindDoubleByDottedPath("usage.output"), 30);
  EXPECT_EQ(run.FindBoolByDottedPath("usage.priced"), false);
  const std::vector<message_center::Notification> shown = Notifications();
  ASSERT_EQ(shown.size(), 1u);
  EXPECT_EQ(shown[0].title(), u"Jarvis desk price");
  EXPECT_EQ(shown[0].context_message(), u"Change alert");
  EXPECT_EQ(shown[0].message(), u"The desk is $479, below your $500.");
  ASSERT_TRUE(chats()->Find(kChat));
  EXPECT_EQ(chats()->Find(kChat)->recent.size(), 1u);
  EXPECT_THAT(model_.Requests().front(),
              HasSubstr("Tell me when the Jarvis desk drops below $500."));
}

IN_PROC_BROWSER_TEST_F(ScheduledChatsBrowserTest,
                       AQuietRunAddsItsLineWithoutANotification) {
  SaveChat();
  model_.Script({Report(false, "Still $549."), ScriptedModel::Answer("Done.")});

  chats()->Save(kChat, DeskWatch(ProviderId()));
  const base::ListValue lines = LinesAfterTheRun();

  ASSERT_EQ(lines.size(), 1u);
  EXPECT_EQ(*lines[0].GetDict().FindString("what"), "");
  EXPECT_TRUE(Notifications().empty());
}

IN_PROC_BROWSER_TEST_F(ScheduledChatsBrowserTest,
                       ARunWithoutAReportTellsOnceAndCounts) {
  SaveChat();
  model_.Script({ScriptedModel::Answer("I looked around.")});

  chats()->Save(kChat, DeskWatch(ProviderId()));
  const base::ListValue lines = LinesAfterTheRun();

  ASSERT_EQ(lines.size(), 1u);
  EXPECT_EQ(*lines[0].GetDict().FindString("end"), "failed");
  EXPECT_EQ(lines[0].GetDict().FindBool("counted"), true);
  EXPECT_EQ(chats()->Find(kChat)->failures, 1);
  ASSERT_EQ(Notifications().size(), 1u);
  EXPECT_EQ(Notifications()[0].context_message(), u"Couldn't check");
}

IN_PROC_BROWSER_TEST_F(ScheduledChatsBrowserTest,
                       AModelThatIsNotSetUpDoesNotCount) {
  SaveChat();

  chats()->Save(kChat, DeskWatch("no-such-provider"));
  const base::ListValue lines = LinesAfterTheRun();

  ASSERT_EQ(lines.size(), 1u);
  EXPECT_EQ(*lines[0].GetDict().FindString("end"), "unreachable");
  EXPECT_EQ(lines[0].GetDict().FindBool("counted"), false);
  EXPECT_EQ(chats()->Find(kChat)->failures, 0);
}

IN_PROC_BROWSER_TEST_F(ScheduledChatsBrowserTest,
                       AChangeWaitsForApprovalAndTheRunGoesOnAfter) {
  SaveChat();
  model_.Script(
      {ScriptedModel::Calls(base::ListValue().Append(ScriptedModel::Call(
           "open", base::DictValue().Set("url", Url("/desk"))))),
       ScriptedModel::Calls(base::ListValue().Append(
           ScriptedModel::Call("read", base::DictValue().Set("tab", 1)))),
       ScriptedModel::Calls(base::ListValue().Append(
           ScriptedModel::Call("click", base::DictValue()
                                            .Set("tab", 1)
                                            .Set("element", 1)
                                            .Set("changes", true)))),
       Report(false, "You did not want to buy it."),
       ScriptedModel::Answer("Done.")});

  chats()->Save(kChat, DeskWatch(ProviderId()));
  ASSERT_TRUE(base::test::RunUntil([&] { return chats()->Waiting(kChat); }));
  ASSERT_EQ(Notifications().size(), 1u);
  EXPECT_EQ(Notifications()[0].context_message(), u"Waits for your approval");
  // Opening the chat brings the tab the step waits in into the user's window.
  TabStripModel* tabs = browser()->tab_strip_model();
  chats()->BringWaitingTab(kChat, *tabs->GetActiveTab());
  ASSERT_EQ(tabs->count(), 2);
  EXPECT_EQ(tabs->GetWebContentsAt(1)->GetVisibleURL().path(), "/desk");
  EXPECT_EQ(tabs->active_index(), 0);

  AgentLoop::ApprovalAnswer answer;
  answer.choice = AgentLoop::ApprovalAnswer::Choice::kDecline;
  chats()->AnswerApproval(kChat, std::move(answer));
  const base::ListValue lines = LinesAfterTheRun();

  ASSERT_EQ(lines.size(), 1u);
  EXPECT_EQ(*lines[0].GetDict().FindString("end"), "reported");
  // It stays with the user after the run ends.
  EXPECT_EQ(tabs->count(), 2);
}

// Every notification a scheduled chat sends is kept as an alert for the
// Inbox, until the chat goes. See docs/product.md, "Scheduled chats".
IN_PROC_BROWSER_TEST_F(ScheduledChatsBrowserTest,
                       ANotificationIsKeptAsAnAlertUntilItsChatGoes) {
  SaveChat();
  const std::string provider = ProviderId();
  model_.Script(
      {Report(true, "Down to $479."), ScriptedModel::Answer("Done.")});
  chats()->Save(kChat, DeskWatch(provider));
  LinesAfterTheRun();

  ChatAlerts* alerts = ChatAlerts::For(profile());
  ASSERT_TRUE(alerts);
  ASSERT_EQ(alerts->alerts().size(), 1u);
  EXPECT_EQ(alerts->alerts()[0].chat_id, kChat);
  EXPECT_EQ(alerts->alerts()[0].what, "Change alert");
  EXPECT_THAT(alerts->alerts()[0].text, HasSubstr("Down to $479."));
  EXPECT_EQ(alerts->Unseen(), 1u);
  // Opening the Inbox sees them all.
  alerts->SeeAll();
  EXPECT_EQ(alerts->Unseen(), 0u);

  // A muted chat sends no notification, so it keeps no alert.
  ScheduledChat muted = DeskWatch(provider);
  muted.muted = true;
  model_.Script(
      {Report(true, "Down to $469."), ScriptedModel::Answer("Done.")});
  chats()->Save(kChat, muted);
  LinesAfterTheRun();
  EXPECT_EQ(alerts->alerts().size(), 1u);

  // Removing the schedule keeps the alert; deleting the chat takes it.
  chats()->Remove(kChat);
  EXPECT_EQ(alerts->alerts().size(), 1u);
  ChatIndex::For(profile())->Delete(kChat);
  EXPECT_TRUE(alerts->alerts().empty());
}

IN_PROC_BROWSER_TEST_F(ScheduledChatsBrowserTest,
                       AnApprovalsAlertGoesOnceItIsAnswered) {
  SaveChat();
  model_.Script(
      {ScriptedModel::Calls(base::ListValue().Append(ScriptedModel::Call(
           "open", base::DictValue().Set("url", Url("/desk"))))),
       ScriptedModel::Calls(base::ListValue().Append(
           ScriptedModel::Call("read", base::DictValue().Set("tab", 1)))),
       ScriptedModel::Calls(base::ListValue().Append(
           ScriptedModel::Call("click", base::DictValue()
                                            .Set("tab", 1)
                                            .Set("element", 1)
                                            .Set("changes", true)))),
       Report(false, "You did not want to buy it."),
       ScriptedModel::Answer("Done.")});
  chats()->Save(kChat, DeskWatch(ProviderId()));
  ASSERT_TRUE(base::test::RunUntil([&] { return chats()->Waiting(kChat); }));

  ChatAlerts* alerts = ChatAlerts::For(profile());
  ASSERT_EQ(alerts->alerts().size(), 1u);
  EXPECT_EQ(alerts->alerts()[0].what, "Waits for your approval");
  EXPECT_TRUE(alerts->alerts()[0].waiting);

  AgentLoop::ApprovalAnswer answer;
  answer.choice = AgentLoop::ApprovalAnswer::Choice::kDecline;
  chats()->AnswerApproval(kChat, std::move(answer));
  EXPECT_TRUE(alerts->alerts().empty());
  // The run found nothing to tell, so nothing else is kept.
  LinesAfterTheRun();
  EXPECT_TRUE(alerts->alerts().empty());
}

IN_PROC_BROWSER_TEST_F(ScheduledChatsBrowserTest,
                       ClickingANotificationSeesItsAlertAndClearEmptiesThem) {
  SaveChat();
  model_.Script({ScriptedModel::Answer("I looked around.")});
  chats()->Save(kChat, DeskWatch(ProviderId()));
  LinesAfterTheRun();

  ChatAlerts* alerts = ChatAlerts::For(profile());
  ASSERT_EQ(alerts->alerts().size(), 1u);
  EXPECT_EQ(alerts->alerts()[0].what, "Couldn't check");
  EXPECT_EQ(alerts->Unseen(), 1u);
  ASSERT_EQ(Notifications().size(), 1u);
  notifications_->SimulateClick(NotificationHandler::Type::UW_CHAT,
                                Notifications()[0].id(), std::nullopt,
                                std::nullopt);
  EXPECT_EQ(alerts->Unseen(), 0u);
  EXPECT_EQ(alerts->alerts().size(), 1u);

  alerts->Clear();
  EXPECT_TRUE(alerts->alerts().empty());
}

// Check now runs a schedule at once, as an extra run: its next time stays,
// and it tells the user what it found like any run.
IN_PROC_BROWSER_TEST_F(ScheduledChatsBrowserTest,
                       CheckNowRunsAtOnceAndKeepsTheNextTime) {
  SaveChat();
  model_.Script({Report(false, "Still $549."), ScriptedModel::Answer("Done.")});
  chats()->Save(kChat, DeskWatch(ProviderId()));
  LinesAfterTheRun();
  const std::optional<base::Time> next = chats()->NextRunOf(kChat);
  ASSERT_TRUE(next);

  model_.Script(
      {Report(true, "Down to $479."), ScriptedModel::Answer("Done.")});
  chats()->CheckNow(kChat);
  EXPECT_TRUE(chats()->Running(kChat));
  const base::ListValue lines = LinesAfterTheRun();

  ASSERT_EQ(lines.size(), 2u);
  EXPECT_EQ(*lines[1].GetDict().FindString("text"), "Down to $479.");
  EXPECT_EQ(chats()->NextRunOf(kChat), next);
  ChatAlerts* alerts = ChatAlerts::For(profile());
  ASSERT_EQ(alerts->alerts().size(), 1u);
  EXPECT_EQ(alerts->alerts()[0].what, "Change alert");
}

// On a paused schedule, Check now runs once and leaves it paused.
IN_PROC_BROWSER_TEST_F(ScheduledChatsBrowserTest,
                       CheckNowLeavesAPausedScheduleAsItIs) {
  SaveChat();
  model_.Script({Report(false, "Still $549."), ScriptedModel::Answer("Done.")});
  chats()->Save(kChat, DeskWatch(ProviderId()));
  LinesAfterTheRun();
  chats()->SetPaused(kChat, true);

  chats()->CheckNow(kChat);
  const base::ListValue lines = LinesAfterTheRun();

  EXPECT_EQ(lines.size(), 2u);
  EXPECT_TRUE(chats()->Find(kChat)->paused);
  EXPECT_FALSE(chats()->NextRunOf(kChat));
}

// Only one run of a chat goes at a time, so Check now starts nothing while a
// run is under way.
IN_PROC_BROWSER_TEST_F(ScheduledChatsBrowserTest,
                       CheckNowStartsNothingWhileARunGoes) {
  SaveChat();
  model_.Script({ScriptedModel::Hang()});
  chats()->Save(kChat, DeskWatch(ProviderId()));
  ASSERT_TRUE(chats()->Running(kChat));

  chats()->CheckNow(kChat);
  ASSERT_TRUE(base::test::RunUntil([&] { return !model_.Requests().empty(); }));
  chats()->Stop(kChat);
  const base::ListValue lines = LinesAfterTheRun();

  ASSERT_EQ(lines.size(), 1u);
  EXPECT_EQ(*lines[0].GetDict().FindString("end"), "stopped");
  EXPECT_EQ(model_.Requests().size(), 1u);
}

// Clicked when no tab shows its chat, as after uw restarts, a notification
// opens the chat.
IN_PROC_BROWSER_TEST_F(ScheduledChatsBrowserTest,
                       ClickingANotificationOpensItsChat) {
  SaveChat();
  model_.Script(
      {Report(true, "Down to $479."), ScriptedModel::Answer("Done.")});
  chats()->Save(kChat, DeskWatch(ProviderId()));
  LinesAfterTheRun();
  ASSERT_TRUE(TabsShowingChat(profile(), kChat).empty());
  ASSERT_EQ(Notifications().size(), 1u);

  notifications_->SimulateClick(NotificationHandler::Type::UW_CHAT,
                                Notifications()[0].id(), std::nullopt,
                                std::nullopt);

  const std::vector<tabs::TabInterface*> showing =
      TabsShowingChat(profile(), kChat);
  ASSERT_EQ(showing.size(), 1u);
  EXPECT_EQ(browser()->tab_strip_model()->GetActiveTab(), showing[0]);
}

IN_PROC_BROWSER_TEST_F(ScheduledChatsBrowserTest,
                       PRE_AlertsLastAcrossARestart) {
  SaveChat();
  model_.Script(
      {Report(true, "Down to $479."), ScriptedModel::Answer("Done.")});
  chats()->Save(kChat, DeskWatch(ProviderId()));
  LinesAfterTheRun();
  ChatAlerts* alerts = ChatAlerts::For(profile());
  ASSERT_TRUE(base::test::RunUntil([&] { return alerts->loaded(); }));
  ASSERT_EQ(alerts->alerts().size(), 1u);
  // Pause it, so that it does not run again when uw starts.
  chats()->SetPaused(kChat, true);
}

IN_PROC_BROWSER_TEST_F(ScheduledChatsBrowserTest, AlertsLastAcrossARestart) {
  ChatAlerts* alerts = ChatAlerts::For(profile());
  ASSERT_TRUE(base::test::RunUntil([&] { return alerts->loaded(); }));
  ASSERT_EQ(alerts->alerts().size(), 1u);
  EXPECT_EQ(alerts->alerts()[0].what, "Change alert");
  EXPECT_EQ(alerts->Unseen(), 1u);
}

IN_PROC_BROWSER_TEST_F(ScheduledChatsBrowserTest, ARunHappensWithNoWindowOpen) {
  SaveChat();
  const std::string provider = ProviderId();
  model_.Script(
      {ScriptedModel::Calls(base::ListValue().Append(ScriptedModel::Call(
           "open", base::DictValue().Set("url", Url("/desk"))))),
       Report(true, "Down to $479."), ScriptedModel::Answer("Done.")});
  ScheduledChats* chats = this->chats();
  Profile* profile = this->profile();
  {
    // Until the schedule's own keep-alive holds the profile.
    ScopedKeepAlive keep_alive(KeepAliveOrigin::SESSION_RESTORE,
                               KeepAliveRestartOption::DISABLED);
    ScopedProfileKeepAlive profile_keep_alive(
        profile, ProfileKeepAliveOrigin::kBrowserWindow);
    CloseBrowserSynchronously(browser());
    chats->Save(kChat, DeskWatch(provider));
  }
  ASSERT_TRUE(base::test::RunUntil([&] { return !chats->Running(kChat); }));
  ASSERT_EQ(chats->Find(kChat)->recent.size(), 1u);
  EXPECT_EQ(chats->Find(kChat)->recent.back().report.message, "Down to $479.");
  SetBrowser(CreateBrowser(profile));
}

IN_PROC_BROWSER_TEST_F(ScheduledChatsBrowserTest,
                       WatchingAPageMakesAChatThatRunsAtOnce) {
  model_.Script(
      {ScriptedModel::Calls(base::ListValue().Append(ScriptedModel::Call(
           "open", base::DictValue().Set("url", Url("/desk"))))),
       Report(true, "Down to $479."), ScriptedModel::Answer("Done.")});
  ScheduledChat watch = DeskWatch(ProviderId());
  watch.page = Url("/desk");
  watch.watch_for = "Price drops below $500";

  const std::string id = chats()->Watch(watch);

  ASSERT_TRUE(chats()->ChatWatching(GURL(Url("/desk?utm_source=news#top"))));
  EXPECT_EQ(*chats()->ChatWatching(GURL(Url("/desk?utm_source=news#top"))), id);
  EXPECT_FALSE(chats()->ChatWatching(GURL(Url("/desk?size=large"))));
  const base::ListValue lines = LinesAfterTheRun(id);
  ASSERT_EQ(lines.size(), 2u);
  EXPECT_EQ(*lines[0].GetDict().FindString("kind"), "user");
  EXPECT_EQ(*lines[0].GetDict().FindString("text"), watch.request);
  EXPECT_EQ(*lines[1].GetDict().FindString("kind"), "run");
  EXPECT_EQ(*lines[1].GetDict().FindString("text"), "Down to $479.");
  const std::vector<ChatIndex::Entry> entries =
      ChatIndex::For(profile())->Entries();
  ASSERT_EQ(entries.size(), 1u);
  EXPECT_EQ(entries[0].id, id);
  EXPECT_EQ(entries[0].title, watch.request);
}

IN_PROC_BROWSER_TEST_F(ScheduledChatsBrowserTest,
                       DeletingTheChatRemovesItsSchedule) {
  SaveChat();
  model_.Script({Report(false, "Still $549."), ScriptedModel::Answer("Done.")});
  chats()->Save(kChat, DeskWatch(ProviderId()));
  LinesAfterTheRun();

  ChatIndex::For(profile())->Delete(kChat);

  EXPECT_FALSE(chats()->Find(kChat));
}

// Writes a schedule last due two days ago, as if uw had been quit since.
IN_PROC_BROWSER_TEST_F(ScheduledChatsBrowserTest,
                       PRE_AMissedRunCatchesUpAfterARestart) {
  // Once the schedules have loaded, the file is read again only at the next
  // start.
  ASSERT_TRUE(base::test::RunUntil([&] { return chats()->loaded(); }));
  SaveChat();
  ScheduledChat chat = DeskWatch(ProviderId());
  chat.last = base::Time::Now() - base::Days(2);
  base::ScopedAllowBlockingForTesting allow_blocking;
  const base::FilePath folder = ChatFile::FolderFor(profile()->GetPath());
  ASSERT_TRUE(base::CreateDirectory(folder));
  ASSERT_TRUE(base::WriteFile(
      folder.AppendASCII("schedules.json"),
      *base::WriteJson(base::DictValue().Set(
          "chats", base::DictValue().Set(kChat, ScheduledChatToValue(chat))))));
}

IN_PROC_BROWSER_TEST_F(ScheduledChatsBrowserTest,
                       AMissedRunCatchesUpAfterARestart) {
  // The first window started the schedules, and the run missed while uw was
  // quit starts at once.
  ASSERT_TRUE(base::test::RunUntil([&] { return chats()->loaded(); }));
  const ScheduledChat* chat = chats()->Find(kChat);
  ASSERT_TRUE(chat);
  EXPECT_TRUE(base::test::RunUntil(
      [&] { return chat->last > base::Time::Now() - base::Hours(1); }));
}

// A trigger runs the user's shell, here without their startup files.
class ScheduledChatTriggersBrowserTest : public ScheduledChatsBrowserTest {
 protected:
  void SetUpOnMainThread() override {
    ScheduledChatsBrowserTest::SetUpOnMainThread();
    base::ScopedAllowBlockingForTesting allow_blocking;
    ASSERT_TRUE(zdotdir_.CreateUniqueTempDir());
    base::Environment::Create()->SetVar("ZDOTDIR", zdotdir_.GetPath().value());
  }

  void TearDownOnMainThread() override {
    ChatTriggers::SetLimitsForTesting(TriggerLimits());
    ScheduledChatsBrowserTest::TearDownOnMainThread();
  }

  // A chat's trigger, as its agent would start it.
  ChatTriggers::Trigger Trigger(std::string command, TriggerWake wake) {
    ChatTriggers::Trigger trigger;
    trigger.command = std::move(command);
    trigger.wake = wake;
    trigger.request = "Tell the user whether the deploy passed.";
    trigger.provider_id = ProviderId();
    trigger.model = "test-agent";
    return trigger;
  }

  // Waits until the chat's trigger and any run on its wake are done, and
  // returns the chat's saved lines.
  base::ListValue LinesOnceItsTriggerEnds() {
    EXPECT_TRUE(base::test::RunUntil([&] {
      return !chats()->TriggerOf(kChat) && !chats()->Running(kChat);
    }));
    return LinesAfterTheRun();
  }

  base::ScopedTempDir zdotdir_;
};

// With the chat closed, a wake starts a run, which tells the user what it
// found and says which trigger woke it.
IN_PROC_BROWSER_TEST_F(ScheduledChatTriggersBrowserTest,
                       AWakeWithTheChatClosedStartsARun) {
  SaveChat();
  model_.Script(
      {Report(true, "The deploy passed."), ScriptedModel::Answer("Reported.")});
  ASSERT_TRUE(chats()
                  ->StartTrigger(
                      kChat, Trigger("echo checks-passed", TriggerWake::kEnd))
                  .has_value());
  EXPECT_TRUE(chats()->TriggerOf(kChat));
  const base::ListValue lines = LinesOnceItsTriggerEnds();

  ASSERT_EQ(lines.size(), 1u);
  const base::DictValue& run = lines[0].GetDict();
  EXPECT_EQ(*run.FindString("kind"), "run");
  EXPECT_EQ(*run.FindString("trigger"), "echo checks-passed");
  EXPECT_EQ(*run.FindString("what"), "Trigger");
  ASSERT_EQ(Notifications().size(), 1u);
  EXPECT_EQ(Notifications()[0].context_message(), u"Trigger");
  EXPECT_EQ(Notifications()[0].message(), u"The deploy passed.");
  const std::string brief = model_.Requests().front();
  EXPECT_THAT(brief, HasSubstr("woke this run"));
  EXPECT_THAT(brief, HasSubstr("checks-passed"));
  EXPECT_THAT(brief, HasSubstr("Tell the user whether the deploy passed."));
}

IN_PROC_BROWSER_TEST_F(ScheduledChatTriggersBrowserTest,
                       ATriggerThatPrintsTooMuchStops) {
  SaveChat();
  ASSERT_TRUE(chats()
                  ->StartTrigger(kChat, Trigger("seq 1 5000; sleep 30",
                                                TriggerWake::kLines))
                  .has_value());
  const base::ListValue lines = LinesOnceItsTriggerEnds();

  ASSERT_FALSE(lines.empty());
  const base::DictValue& stopped = lines.back().GetDict();
  EXPECT_EQ(*stopped.FindString("kind"), "trigger_stopped");
  EXPECT_EQ(*stopped.FindString("command"), "seq 1 5000; sleep 30");
  EXPECT_EQ(*stopped.FindString("why"), "too_much");
}

IN_PROC_BROWSER_TEST_F(ScheduledChatTriggersBrowserTest,
                       AChatsTriggerStopsAfterADay) {
  TriggerLimits limits;
  limits.life = base::Seconds(1);
  ChatTriggers::SetLimitsForTesting(limits);
  SaveChat();
  ASSERT_TRUE(chats()
                  ->StartTrigger(kChat, Trigger("sleep 30", TriggerWake::kEnd))
                  .has_value());
  const base::ListValue lines = LinesOnceItsTriggerEnds();

  ASSERT_EQ(lines.size(), 1u);
  EXPECT_EQ(*lines[0].GetDict().FindString("why"), "day");
}

// Saved on the schedule card, the chat's trigger lasts, and its wake starts a
// run of the schedule once the run under way ends.
IN_PROC_BROWSER_TEST_F(ScheduledChatTriggersBrowserTest,
                       TheScheduleCardKeepsAChatsTrigger) {
  SaveChat();
  model_.Script(
      {Report(false, "Still building."), ScriptedModel::Answer("Done."),
       Report(true, "The build is done."), ScriptedModel::Answer("Reported.")});
  ASSERT_TRUE(chats()
                  ->StartTrigger(
                      kChat, Trigger("sleep 2; echo built", TriggerWake::kEnd))
                  .has_value());
  ScheduledChat chat = DeskWatch(ProviderId());
  chat.trigger = "sleep 2; echo built";
  chats()->Save(kChat, chat);
  ASSERT_TRUE(chats()->TriggerOf(kChat));
  EXPECT_TRUE(chats()->TriggerOf(kChat)->lasting);
  EXPECT_EQ(chats()->Find(kChat)->trigger_wake, "end");

  const base::ListValue lines = LinesOnceItsTriggerEnds();
  ASSERT_EQ(lines.size(), 2u);
  EXPECT_EQ(*lines[0].GetDict().FindString("trigger"), "");
  EXPECT_EQ(*lines[1].GetDict().FindString("trigger"), "sleep 2; echo built");
  EXPECT_EQ(*lines[1].GetDict().FindString("what"), "Change alert");
  EXPECT_EQ(chats()->Find(kChat)->trigger, "sleep 2; echo built");
}

IN_PROC_BROWSER_TEST_F(ScheduledChatTriggersBrowserTest,
                       PRE_ALastingTriggerStartsAgainWhenUwLaunches) {
  SaveChat();
  model_.Script(
      {Report(false, "Nothing yet."), ScriptedModel::Answer("Done.")});
  ASSERT_TRUE(chats()
                  ->StartTrigger(kChat, Trigger("sleep 600", TriggerWake::kEnd))
                  .has_value());
  ScheduledChat chat = DeskWatch(ProviderId());
  chat.trigger = "sleep 600";
  chats()->Save(kChat, chat);
  LinesAfterTheRun();
  ASSERT_TRUE(chats()->TriggerOf(kChat));
  EXPECT_TRUE(chats()->TriggerOf(kChat)->lasting);
}

IN_PROC_BROWSER_TEST_F(ScheduledChatTriggersBrowserTest,
                       ALastingTriggerStartsAgainWhenUwLaunches) {
  ASSERT_TRUE(base::test::RunUntil([&] { return chats()->loaded(); }));
  const ChatTriggers::Trigger* trigger = chats()->TriggerOf(kChat);
  ASSERT_TRUE(trigger);
  EXPECT_EQ(trigger->command, "sleep 600");
  EXPECT_TRUE(trigger->lasting);

  // Pause stops it, and Resume starts it again.
  chats()->SetPaused(kChat, true);
  EXPECT_FALSE(chats()->TriggerOf(kChat));
  chats()->SetPaused(kChat, false);
  EXPECT_TRUE(chats()->TriggerOf(kChat));
}

}  // namespace
}  // namespace uw
