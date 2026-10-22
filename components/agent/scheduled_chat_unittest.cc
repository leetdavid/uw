// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/components/agent/scheduled_chat.h"

#include <string>

#include "testing/gtest/include/gtest/gtest.h"

namespace uw {
namespace {

base::Time Utc(int day, int hour = 16) {
  base::Time::Exploded exploded = {};
  exploded.year = 2026;
  exploded.month = 10;
  exploded.day_of_month = day;
  exploded.hour = hour;
  base::Time time;
  EXPECT_TRUE(base::Time::FromUTCExploded(exploded, &time));
  return time;
}

ScheduledChat DeskWatch() {
  ScheduledChat chat;
  chat.schedule.repeat = Schedule::Repeat::kDaily;
  chat.schedule.hour = 9;
  chat.request = "Tell me when the Jarvis desk drops below $500.";
  return chat;
}

RunReport Report(bool found, std::string message) {
  return {found, std::move(message), {{"price", "USD", 549.0}}, ""};
}

TEST(ScheduledChatTest, KeepsEverythingItSaves) {
  ScheduledChat chat = DeskWatch();
  chat.schedule.time_zone = "America/Los_Angeles";
  chat.even_when_nothing_changes = true;
  chat.provider_id = "deepseek";
  chat.model = "deepseek-flash";
  chat.approval_mode = "full_auto";
  chat.muted = true;
  chat.last = Utc(22);
  chat.failures = 1;
  chat.failing = true;
  chat.recent = {
      {Utc(21), Report(false, "Still above $500.")},
      {Utc(22), {true, "Down to $479.", {{"in_stock", "", true}}, ""}}};

  EXPECT_EQ(ScheduledChatFromValue(ScheduledChatToValue(chat)), chat);
}

TEST(ScheduledChatTest, RefusesWhatTheCardCouldNotHaveSaved) {
  ScheduledChat chat = DeskWatch();
  chat.request.clear();
  EXPECT_EQ(ScheduledChatFromValue(ScheduledChatToValue(chat)), std::nullopt);
  base::DictValue unknown =
      ScheduledChatToValue(DeskWatch()).Set("repeat", "fortnightly");
  EXPECT_EQ(ScheduledChatFromValue(unknown), std::nullopt);
}

TEST(ScheduledChatTest, ChangeAlertsTellOnlyWhatWasAskedFor) {
  ScheduledChat chat = DeskWatch();
  EXPECT_EQ(AfterRun(chat, RunEnd::kReported, Report(false, "Still $549."), "",
                     Utc(22))
                .what,
            "");
  const RunEffect found = AfterRun(chat, RunEnd::kReported,
                                   Report(true, "Down to $479."), "", Utc(23));
  EXPECT_EQ(found.what, "Change alert");
  EXPECT_EQ(found.text, "Down to $479.");

  chat.even_when_nothing_changes = true;
  EXPECT_EQ(AfterRun(chat, RunEnd::kReported, Report(false, "Still $479."), "",
                     Utc(24))
                .what,
            "No change");
}

TEST(ScheduledChatTest, RegularUpdatesTellEveryRun) {
  ScheduledChat chat = DeskWatch();
  chat.regular_updates = true;
  EXPECT_EQ(AfterRun(chat, RunEnd::kReported, Report(false, "Still $549."), "",
                     Utc(22))
                .what,
            "Update");
}

TEST(ScheduledChatTest, KeepsTheLatestReports) {
  ScheduledChat chat = DeskWatch();
  for (int day = 18; day <= 22; ++day) {
    AfterRun(chat, RunEnd::kReported, Report(false, "Still $549."), "",
             Utc(day));
  }
  ASSERT_EQ(chat.recent.size(), kPastRuns);
  EXPECT_EQ(chat.recent.front().time, Utc(20));
  EXPECT_EQ(chat.recent.back().time, Utc(22));
}

TEST(ScheduledChatTest, AStreakOfFailuresTellsOnceAndPausesAtThree) {
  ScheduledChat chat = DeskWatch();
  const RunEffect first =
      AfterRun(chat, RunEnd::kFailed, {}, "fully.com didn't load", Utc(20));
  EXPECT_EQ(first.what, "Couldn't check");
  EXPECT_TRUE(first.counted);
  EXPECT_EQ(
      AfterRun(chat, RunEnd::kFailed, {}, "fully.com didn't load", Utc(21))
          .what,
      "");
  const RunEffect third =
      AfterRun(chat, RunEnd::kFailed, {}, "fully.com didn't load", Utc(22));
  EXPECT_TRUE(third.paused);
  EXPECT_EQ(third.what, "Paused");
  EXPECT_EQ(third.text, "Paused after 3 failed runs: fully.com didn't load");
  EXPECT_TRUE(chat.paused);
  EXPECT_EQ(chat.pause_reason, "fully.com didn't load");
}

TEST(ScheduledChatTest, AModelThatCannotBeReachedNeitherCountsNorEndsAStreak) {
  ScheduledChat chat = DeskWatch();
  AfterRun(chat, RunEnd::kFailed, {}, "fully.com didn't load", Utc(19));
  const RunEffect offline =
      AfterRun(chat, RunEnd::kModelUnreachable, {}, "HTTP 503", Utc(20));
  EXPECT_FALSE(offline.counted);
  EXPECT_EQ(offline.what, "");
  EXPECT_EQ(chat.failures, 1);
  AfterRun(chat, RunEnd::kFailed, {}, "fully.com didn't load", Utc(21));
  EXPECT_FALSE(chat.paused);
  EXPECT_TRUE(
      AfterRun(chat, RunEnd::kFailed, {}, "fully.com didn't load", Utc(22))
          .paused);
}

TEST(ScheduledChatTest, ASuccessEndsAStreak) {
  ScheduledChat chat = DeskWatch();
  AfterRun(chat, RunEnd::kFailed, {}, "fully.com didn't load", Utc(20));
  AfterRun(chat, RunEnd::kFailed, {}, "fully.com didn't load", Utc(21));
  AfterRun(chat, RunEnd::kReported, Report(false, "Still $549."), "", Utc(22));
  EXPECT_EQ(chat.failures, 0);
  EXPECT_EQ(
      AfterRun(chat, RunEnd::kFailed, {}, "fully.com didn't load", Utc(23))
          .what,
      "Couldn't check");
}

TEST(ScheduledChatTest, ASiteThatWantsASignInIsACountedFailure) {
  ScheduledChat chat = DeskWatch();
  RunReport report = Report(false, "fully.com wants me to sign in.");
  report.sign_in = "fully.com";
  const RunEffect effect =
      AfterRun(chat, RunEnd::kReported, report, "", Utc(22));
  EXPECT_EQ(effect.what, "Sign in required");
  EXPECT_EQ(effect.text, "Sign in required on fully.com");
  EXPECT_TRUE(effect.counted);
  EXPECT_TRUE(chat.recent.empty());
}

TEST(ScheduledChatTest, MuteSilencesButKeepsCounting) {
  ScheduledChat chat = DeskWatch();
  chat.muted = true;
  EXPECT_EQ(AfterRun(chat, RunEnd::kReported, Report(true, "Down to $479."), "",
                     Utc(22))
                .what,
            "");
  for (int day = 23; day <= 25; ++day) {
    AfterRun(chat, RunEnd::kFailed, {}, "fully.com didn't load", Utc(day));
  }
  EXPECT_TRUE(chat.paused);
}

TEST(ScheduledChatTest, AStoppedRunChangesNothing) {
  ScheduledChat chat = DeskWatch();
  const ScheduledChat before = chat;
  EXPECT_EQ(AfterRun(chat, RunEnd::kStopped, {}, "", Utc(22)).what, "");
  EXPECT_EQ(chat, before);
}

}  // namespace
}  // namespace uw
