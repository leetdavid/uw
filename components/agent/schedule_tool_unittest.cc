// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/components/agent/schedule_tool.h"

#include <string>
#include <string_view>

#include "base/json/json_reader.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace uw {
namespace {

base::DictValue Args(std::string_view json) {
  return base::JSONReader::ReadDict(json, base::JSON_PARSE_RFC).value();
}

base::Time Local(int year, int month, int day, int hour, int minute = 0) {
  base::Time::Exploded exploded = {};
  exploded.year = year;
  exploded.month = month;
  exploded.day_of_month = day;
  exploded.hour = hour;
  exploded.minute = minute;
  base::Time time;
  EXPECT_TRUE(base::Time::FromLocalExploded(exploded, &time));
  return time;
}

// A new card, as it opens: the chat's first message, daily at 9:00, change
// alerts, and the chat's model.
ScheduledChat NewCard() {
  ScheduledChat card;
  card.request = "Is the desk on sale?";
  card.model = "deepseek-flash";
  return card;
}

// 6:24 PM on Thursday, Oct 22, 2026.
base::Time Now() {
  return Local(2026, 10, 22, 18, 24);
}

TEST(ScheduleToolTest, FillsInTheCardFromTheUsersWords) {
  const base::expected<ScheduledChat, std::string> card = FillScheduleCard(
      Args(R"({"request": "  Check fully.com/desk for a price drop  ",
               "repeat": "daily", "time": "08:30"})"),
      NewCard(), Now());
  ASSERT_TRUE(card.has_value()) << card.error();
  EXPECT_EQ(card->request, "Check fully.com/desk for a price drop");
  EXPECT_EQ(card->schedule.repeat, Schedule::Repeat::kDaily);
  EXPECT_EQ(card->schedule.hour, 8);
  EXPECT_EQ(card->schedule.minute, 30);
  EXPECT_FALSE(card->regular_updates);
  // What the call does not say stays as the card has it.
  EXPECT_EQ(card->model, "deepseek-flash");
  EXPECT_TRUE(card->schedule.time_zone.empty());
}

TEST(ScheduleToolTest, TakesEachKindOfRepeat) {
  base::expected<ScheduledChat, std::string> card = FillScheduleCard(
      Args(R"({"request": "News on Warp", "repeat": "every_hours",
               "hours": 6, "regular_updates": true})"),
      NewCard(), Now());
  ASSERT_TRUE(card.has_value()) << card.error();
  EXPECT_EQ(card->schedule.repeat, Schedule::Repeat::kEveryHours);
  EXPECT_EQ(card->schedule.hours, 6);
  EXPECT_EQ(card->schedule.start, Now());
  EXPECT_TRUE(card->regular_updates);

  card = FillScheduleCard(Args(R"({"request": "Standup notes",
                                   "repeat": "weekdays", "time": "9:00"})"),
                          NewCard(), Now());
  ASSERT_TRUE(card.has_value()) << card.error();
  EXPECT_EQ(card->schedule.repeat, Schedule::Repeat::kWeekdays);
  EXPECT_EQ(card->schedule.hour, 9);

  card = FillScheduleCard(Args(R"({"request": "Weekly report",
                                   "repeat": "weekly", "time": "17:00",
                                   "weekday": "Friday"})"),
                          NewCard(), Now());
  ASSERT_TRUE(card.has_value()) << card.error();
  EXPECT_EQ(card->schedule.repeat, Schedule::Repeat::kWeekly);
  EXPECT_EQ(card->schedule.weekday, 5);
  EXPECT_EQ(card->schedule.hour, 17);

  card = FillScheduleCard(Args(R"({"request": "Remind me of the dentist",
                                   "repeat": "once", "time": "15:00",
                                   "date": "2026-10-24"})"),
                          NewCard(), Now());
  ASSERT_TRUE(card.has_value()) << card.error();
  EXPECT_EQ(card->schedule.repeat, Schedule::Repeat::kOnce);
  EXPECT_EQ(card->schedule.start, Local(2026, 10, 24, 15));
}

TEST(ScheduleToolTest, SaysWhatIsWrongWithACall) {
  const auto error = [](std::string_view json) {
    base::expected<ScheduledChat, std::string> card =
        FillScheduleCard(Args(json), NewCard(), Now());
    return card.has_value() ? std::string() : card.error();
  };
  EXPECT_EQ(error(R"({"request": " ", "repeat": "daily", "time": "09:00"})"),
            "schedule needs the request each run is asked.");
  EXPECT_EQ(error(R"({"request": "x", "repeat": "every_minute"})"),
            "repeat must be every_hours, daily, weekdays, weekly, or once.");
  EXPECT_EQ(error(R"({"request": "x", "repeat": "every_hours", "hours": 5})"),
            "hours must be 1, 2, 3, 4, 6, or 12.");
  EXPECT_EQ(error(R"({"request": "x", "repeat": "daily", "time": "9am"})"),
            "time must be 24-hour, such as 09:00.");
  EXPECT_EQ(error(R"({"request": "x", "repeat": "daily", "time": "24:00"})"),
            "time must be 24-hour, such as 09:00.");
  EXPECT_EQ(error(R"({"request": "x", "repeat": "weekly", "time": "09:00",
                      "weekday": "someday"})"),
            "weekday must be monday to sunday.");
  EXPECT_EQ(error(R"({"request": "x", "repeat": "once", "time": "09:00",
                      "date": "tomorrow"})"),
            "date must be a real day, such as 2026-10-24.");
  EXPECT_EQ(error(R"({"request": "x", "repeat": "once", "time": "09:00",
                      "date": "2026-10-22"})"),
            "That time has passed.");
}

}  // namespace
}  // namespace uw
