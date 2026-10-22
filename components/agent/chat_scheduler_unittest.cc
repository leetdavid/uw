// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/components/agent/chat_scheduler.h"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "base/test/icu_test_util.h"
#include "base/test/power_monitor_test.h"
#include "base/test/simple_test_clock.h"
#include "base/test/task_environment.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace uw {
namespace {

using Fired = std::pair<std::string, base::Time>;

base::Time Utc(int year, int month, int day, int hour, int minute = 0) {
  base::Time::Exploded exploded = {};
  exploded.year = year;
  exploded.month = month;
  exploded.day_of_month = day;
  exploded.hour = hour;
  exploded.minute = minute;
  base::Time time;
  EXPECT_TRUE(base::Time::FromUTCExploded(exploded, &time));
  return time;
}

Schedule DailyAt(int hour, int minute = 0) {
  Schedule schedule;
  schedule.repeat = Schedule::Repeat::kDaily;
  schedule.hour = hour;
  schedule.minute = minute;
  return schedule;
}

// Wall time is a test clock that starts at 8:00 in Los Angeles on Thursday
// 2026-10-22, 15:00 UTC, and moves with the task environment's ticks.
class ChatSchedulerTest : public testing::Test {
 protected:
  ChatSchedulerTest() { clock_.SetNow(Utc(2026, 10, 22, 15)); }

  // Moves wall time and ticks together a minute at a time, so each timer
  // fires at the wall time it waited for.
  void FastForwardTo(base::Time time) {
    while (clock_.Now() < time) {
      const base::TimeDelta step =
          std::min(base::Minutes(1), time - clock_.Now());
      clock_.Advance(step);
      task_environment_.FastForwardBy(step);
    }
  }

  base::test::ScopedRestoreDefaultTimezone zone_{"America/Los_Angeles"};
  base::test::ScopedPowerMonitorTestSource power_;
  base::test::SingleThreadTaskEnvironment task_environment_{
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};
  base::SimpleTestClock clock_;
  std::vector<Fired> runs_;
  ChatScheduler scheduler_{
      base::BindRepeating(
          [](std::vector<Fired>* runs, const std::string& id, base::Time when) {
            runs->emplace_back(id, when);
          },
          &runs_),
      &clock_, task_environment_.GetMockTickClock()};
};

TEST_F(ChatSchedulerTest, ADueChatRunsOnTime) {
  scheduler_.Set("jarvis", DailyAt(9), Utc(2026, 10, 21, 16));
  FastForwardTo(Utc(2026, 10, 22, 15, 59));
  EXPECT_TRUE(runs_.empty());
  FastForwardTo(Utc(2026, 10, 22, 16));
  EXPECT_EQ(runs_, std::vector<Fired>({{"jarvis", Utc(2026, 10, 22, 16)}}));
  FastForwardTo(Utc(2026, 10, 23, 16));
  EXPECT_EQ(runs_.size(), 2u);
}

TEST_F(ChatSchedulerTest, MissedRunsMakeOneCatchUpRunAtOnce) {
  scheduler_.Set("jarvis", DailyAt(9), Utc(2026, 10, 19, 16));
  task_environment_.RunUntilIdle();
  EXPECT_EQ(runs_, std::vector<Fired>({{"jarvis", Utc(2026, 10, 22, 15)}}));
}

TEST_F(ChatSchedulerTest, RunsMissedInSleepMakeOneCatchUpRunOnWaking) {
  scheduler_.Set("jarvis", DailyAt(9), Utc(2026, 10, 21, 16));
  power_.GenerateSuspendEvent();
  clock_.Advance(base::Hours(26));
  power_.GenerateResumeEvent();
  task_environment_.RunUntilIdle();
  EXPECT_EQ(runs_, std::vector<Fired>({{"jarvis", Utc(2026, 10, 23, 17)}}));
}

TEST_F(ChatSchedulerTest, ARemovedChatNeverRuns) {
  scheduler_.Set("jarvis", DailyAt(9), Utc(2026, 10, 21, 16));
  scheduler_.Remove("jarvis");
  FastForwardTo(Utc(2026, 10, 24, 0));
  EXPECT_TRUE(runs_.empty());
}

TEST_F(ChatSchedulerTest, EachChatRunsOnItsOwnTime) {
  scheduler_.Set("jarvis", DailyAt(9), Utc(2026, 10, 21, 16));
  scheduler_.Set("briefing", DailyAt(7, 30), Utc(2026, 10, 22, 14, 30));
  FastForwardTo(Utc(2026, 10, 23, 15));
  EXPECT_EQ(runs_,
            std::vector<Fired>({{"jarvis", Utc(2026, 10, 22, 16)},
                                {"briefing", Utc(2026, 10, 23, 14, 30)}}));
}

TEST_F(ChatSchedulerTest, ANewTimeZoneMovesTheNextRun) {
  scheduler_.Set("jarvis", DailyAt(9), Utc(2026, 10, 21, 16));
  // 9:00 in New York has passed today, so moving there makes a catch-up run
  // now, and the next run is 9:00 there, not in Los Angeles.
  base::test::ScopedRestoreDefaultTimezone new_york("America/New_York");
  scheduler_.TimeZoneChanged();
  task_environment_.RunUntilIdle();
  FastForwardTo(Utc(2026, 10, 23, 13));
  EXPECT_EQ(runs_, std::vector<Fired>({{"jarvis", Utc(2026, 10, 22, 15)},
                                       {"jarvis", Utc(2026, 10, 23, 13)}}));
}

}  // namespace
}  // namespace uw
