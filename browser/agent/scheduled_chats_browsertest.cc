// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/browser/agent/scheduled_chats.h"

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "base/files/file_util.h"
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
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "components/prefs/pref_service.h"
#include "content/public/test/browser_test.h"
#include "net/dns/mock_host_resolver.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "net/test/embedded_test_server/http_request.h"
#include "net/test/embedded_test_server/http_response.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "ui/message_center/public/cpp/notification.h"
#include "uw/browser/agent/chat_file.h"
#include "uw/browser/agent/chat_index.h"
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
  base::ListValue LinesAfterTheRun() {
    EXPECT_TRUE(base::test::RunUntil(
        [&] { return chats()->loaded() && !chats()->Running(kChat); }));
    base::test::TestFuture<std::optional<base::DictValue>> loaded;
    ChatFile(profile()->GetPath(), kChat).Load(loaded.GetCallback());
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
       Report(true, "The desk is $479, below your $500."),
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

  AgentLoop::ApprovalAnswer answer;
  answer.choice = AgentLoop::ApprovalAnswer::Choice::kDecline;
  chats()->AnswerApproval(kChat, std::move(answer));
  const base::ListValue lines = LinesAfterTheRun();

  ASSERT_EQ(lines.size(), 1u);
  EXPECT_EQ(*lines[0].GetDict().FindString("end"), "reported");
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

}  // namespace
}  // namespace uw
