// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/components/agent/schedule.h"

#include "base/test/icu_test_util.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace uw {
namespace {

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

Schedule At(Schedule::Repeat repeat, int hour, int minute = 0) {
  Schedule schedule;
  schedule.repeat = repeat;
  schedule.hour = hour;
  schedule.minute = minute;
  schedule.time_zone = "America/Los_Angeles";
  return schedule;
}

// Los Angeles is UTC-7 until 2026-11-01 and UTC-8 after it. 2026-10-22 is a
// Thursday.

TEST(ScheduleTest, EveryDayRunsAtItsWallTime) {
  Schedule daily = At(Schedule::Repeat::kDaily, 9);
  EXPECT_EQ(NextRun(daily, Utc(2026, 10, 22, 15, 59)), Utc(2026, 10, 22, 16));
  EXPECT_EQ(NextRun(daily, Utc(2026, 10, 22, 16)), Utc(2026, 10, 23, 16));
}

TEST(ScheduleTest, WeekdaysSkipTheWeekend) {
  Schedule weekdays = At(Schedule::Repeat::kWeekdays, 7, 30);
  EXPECT_EQ(NextRun(weekdays, Utc(2026, 10, 23, 15)),
            Utc(2026, 10, 26, 14, 30));
}

TEST(ScheduleTest, EveryWeekRunsOnItsDay) {
  Schedule weekly = At(Schedule::Repeat::kWeekly, 9);
  weekly.weekday = 1;
  EXPECT_EQ(NextRun(weekly, Utc(2026, 10, 22, 16)), Utc(2026, 10, 26, 16));
  EXPECT_EQ(NextRun(weekly, Utc(2026, 10, 26, 16)), Utc(2026, 11, 2, 17));
}

TEST(ScheduleTest, NineStaysNineAcrossDaylightSaving) {
  Schedule daily = At(Schedule::Repeat::kDaily, 9);
  EXPECT_EQ(NextRun(daily, Utc(2026, 10, 31, 17)), Utc(2026, 11, 1, 17));
  EXPECT_EQ(NextRun(daily, Utc(2026, 3, 7, 18)), Utc(2026, 3, 8, 16));
}

TEST(ScheduleTest, ASkippedWallTimeRunsWhenTheClockJumpsPastIt) {
  // 2:30 never happens on 2026-03-08: the clock goes from 2:00 to 3:00 PDT.
  Schedule daily = At(Schedule::Repeat::kDaily, 2, 30);
  EXPECT_EQ(NextRun(daily, Utc(2026, 3, 7, 20)), Utc(2026, 3, 8, 10));
  EXPECT_EQ(NextRun(daily, Utc(2026, 3, 8, 10)), Utc(2026, 3, 9, 9, 30));
}

TEST(ScheduleTest, ARepeatedWallTimeRunsOnce) {
  // 1:30 comes twice on 2026-11-01, first in PDT and then in PST.
  Schedule daily = At(Schedule::Repeat::kDaily, 1, 30);
  EXPECT_EQ(NextRun(daily, Utc(2026, 10, 31, 20)), Utc(2026, 11, 1, 8, 30));
  EXPECT_EQ(NextRun(daily, Utc(2026, 11, 1, 8, 30)), Utc(2026, 11, 2, 9, 30));
}

TEST(ScheduleTest, NoTimeZoneFollowsTheMacsZone) {
  Schedule daily = At(Schedule::Repeat::kDaily, 9);
  daily.time_zone.clear();
  {
    base::test::ScopedRestoreDefaultTimezone tokyo("Asia/Tokyo");
    EXPECT_EQ(NextRun(daily, Utc(2026, 10, 22, 1)), Utc(2026, 10, 23, 0));
  }
  {
    base::test::ScopedRestoreDefaultTimezone london("Europe/London");
    EXPECT_EQ(NextRun(daily, Utc(2026, 10, 22, 1)), Utc(2026, 10, 22, 8));
  }
}

TEST(ScheduleTest, EveryFewHoursCountsFromTheStart) {
  Schedule every;
  every.repeat = Schedule::Repeat::kEveryHours;
  every.hours = 6;
  every.start = Utc(2026, 10, 22, 14, 23);
  EXPECT_EQ(NextRun(every, Utc(2026, 10, 22, 10)), Utc(2026, 10, 22, 14, 23));
  EXPECT_EQ(NextRun(every, Utc(2026, 10, 22, 14, 23)),
            Utc(2026, 10, 22, 20, 23));
  EXPECT_EQ(NextRun(every, Utc(2026, 10, 23, 3)), Utc(2026, 10, 23, 8, 23));
}

TEST(ScheduleTest, OnceRunsOnce) {
  Schedule once;
  once.repeat = Schedule::Repeat::kOnce;
  once.start = Utc(2026, 10, 23, 22);
  EXPECT_EQ(NextRun(once, Utc(2026, 10, 22, 16)), Utc(2026, 10, 23, 22));
  EXPECT_EQ(NextRun(once, Utc(2026, 10, 23, 22)), std::nullopt);
}

TEST(ScheduleTest, MissedRunsMakeOneCatchUpRun) {
  Schedule daily = At(Schedule::Repeat::kDaily, 9);
  EXPECT_TRUE(IsDue(daily, Utc(2026, 10, 20, 16), Utc(2026, 10, 22, 22)));
  EXPECT_FALSE(IsDue(daily, Utc(2026, 10, 22, 22), Utc(2026, 10, 22, 23)));
  EXPECT_FALSE(IsDue(daily, Utc(2026, 10, 22, 16), Utc(2026, 10, 22, 22)));
}

TEST(ScheduleTest, OnlyTheCardsChoicesAreValid) {
  Schedule every;
  every.repeat = Schedule::Repeat::kEveryHours;
  every.start = Utc(2026, 10, 22, 14);
  every.hours = 12;
  EXPECT_TRUE(IsValid(every));
  every.hours = 5;
  EXPECT_FALSE(IsValid(every));

  Schedule daily = At(Schedule::Repeat::kDaily, 9);
  EXPECT_TRUE(IsValid(daily));
  daily.minute = 60;
  EXPECT_FALSE(IsValid(daily));
  daily.minute = 0;
  daily.time_zone = "Mars/Olympus_Mons";
  EXPECT_FALSE(IsValid(daily));
  EXPECT_EQ(NextRun(daily, Utc(2026, 10, 22, 0)), std::nullopt);

  Schedule weekly = At(Schedule::Repeat::kWeekly, 9);
  weekly.weekday = 7;
  EXPECT_FALSE(IsValid(weekly));

  Schedule once;
  once.repeat = Schedule::Repeat::kOnce;
  EXPECT_FALSE(IsValid(once));
}

}  // namespace
}  // namespace uw
