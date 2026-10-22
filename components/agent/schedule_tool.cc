// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/components/agent/schedule_tool.h"

#include <algorithm>
#include <iterator>
#include <string_view>
#include <utility>
#include <vector>

#include "base/strings/string_number_conversions.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"

namespace uw {

namespace {

constexpr std::pair<std::string_view, Schedule::Repeat> kRepeats[] = {
    {"every_hours", Schedule::Repeat::kEveryHours},
    {"daily", Schedule::Repeat::kDaily},
    {"weekdays", Schedule::Repeat::kWeekdays},
    {"weekly", Schedule::Repeat::kWeekly},
    {"once", Schedule::Repeat::kOnce},
};

// In base::Time::Exploded's order, from Sunday.
constexpr std::string_view kWeekdays[] = {"sunday",    "monday",   "tuesday",
                                          "wednesday", "thursday", "friday",
                                          "saturday"};

base::DictValue Parameter(std::string_view type, std::string_view description) {
  return base::DictValue().Set("type", type).Set("description", description);
}

// The numbers of `text` split at `separator`, such as 9 and 30 for "09:30".
std::vector<int> NumbersIn(std::string_view text, char separator) {
  std::vector<int> numbers;
  for (std::string_view part :
       base::SplitStringPiece(text, std::string_view(&separator, 1),
                              base::TRIM_WHITESPACE, base::SPLIT_WANT_ALL)) {
    int number = 0;
    if (!base::StringToInt(part, &number)) {
      return {};
    }
    numbers.push_back(number);
  }
  return numbers;
}

}  // namespace

base::DictValue ScheduleToolSchema() {
  base::DictValue properties =
      base::DictValue()
          .Set("request",
               Parameter("string",
                         "What each run is asked, in the user's words, with "
                         "the page's address when it is about a page."))
          .Set("repeat",
               Parameter("string",
                         "One of every_hours, daily, weekdays (Monday to "
                         "Friday), weekly, or once."))
          .Set("hours",
               Parameter("integer",
                         "For every_hours: 1, 2, 3, 4, 6, or 12. Hourly is the "
                         "shortest."))
          .Set("time", Parameter("string",
                                 "For daily, weekdays, weekly, and once: the "
                                 "time of day, 24-hour, such as 09:00."))
          .Set("weekday", Parameter("string", "For weekly: monday to sunday."))
          .Set("date", Parameter("string",
                                 "For once: the day, such as "
                                 "2026-10-24."))
          .Set("regular_updates",
               Parameter("boolean",
                         "True to tell the user what every run found. Leave "
                         "out to tell them only when a run finds what they "
                         "asked to hear about."));
  return base::DictValue()
      .Set("type", "function")
      .Set("function",
           base::DictValue()
               .Set("name", kScheduleTool)
               .Set("description",
                    "Fills in the schedule card for this chat when the user "
                    "asks for something to happen on a schedule, such as "
                    "checking a page every morning. The user saves it; "
                    "nothing is scheduled until they do.")
               .Set("parameters",
                    base::DictValue()
                        .Set("type", "object")
                        .Set("properties", std::move(properties))
                        .Set("required",
                             base::ListValue().Append("request").Append(
                                 "repeat"))));
}

base::expected<ScheduledChat, std::string> FillScheduleCard(
    const base::DictValue& args,
    ScheduledChat card,
    base::Time now) {
  const std::string* request = args.FindString("request");
  const std::string_view trimmed =
      request ? base::TrimWhitespaceASCII(*request, base::TRIM_ALL)
              : std::string_view();
  if (trimmed.empty()) {
    return base::unexpected("schedule needs the request each run is asked.");
  }
  const std::string* repeat = args.FindString("repeat");
  const auto* named = std::ranges::find_if(kRepeats, [&](const auto& entry) {
    return repeat && entry.first == *repeat;
  });
  if (named == std::end(kRepeats)) {
    return base::unexpected(
        "repeat must be every_hours, daily, weekdays, weekly, or once.");
  }
  Schedule& schedule = card.schedule;
  schedule.repeat = named->second;
  schedule.start = now;
  if (schedule.repeat == Schedule::Repeat::kEveryHours) {
    schedule.hours = args.FindInt("hours").value_or(0);
    if (!IsValid(schedule)) {
      return base::unexpected("hours must be 1, 2, 3, 4, 6, or 12.");
    }
  } else {
    const std::string* time = args.FindString("time");
    const std::vector<int> clock =
        time ? NumbersIn(*time, ':') : std::vector<int>();
    if (clock.size() != 2 || clock[0] < 0 || clock[0] > 23 || clock[1] < 0 ||
        clock[1] > 59) {
      return base::unexpected("time must be 24-hour, such as 09:00.");
    }
    schedule.hour = clock[0];
    schedule.minute = clock[1];
  }
  if (schedule.repeat == Schedule::Repeat::kWeekly) {
    const std::string* weekday = args.FindString("weekday");
    const auto* day = std::ranges::find(
        kWeekdays, weekday ? base::ToLowerASCII(*weekday) : std::string());
    if (day == std::end(kWeekdays)) {
      return base::unexpected("weekday must be monday to sunday.");
    }
    schedule.weekday = static_cast<int>(day - std::begin(kWeekdays));
  }
  if (schedule.repeat == Schedule::Repeat::kOnce) {
    const std::string* date = args.FindString("date");
    const std::vector<int> day =
        date ? NumbersIn(*date, '-') : std::vector<int>();
    base::Time::Exploded exploded = {};
    if (day.size() == 3) {
      exploded.year = day[0];
      exploded.month = day[1];
      exploded.day_of_month = day[2];
      exploded.hour = schedule.hour;
      exploded.minute = schedule.minute;
    }
    if (!base::Time::FromLocalExploded(exploded, &schedule.start)) {
      return base::unexpected("date must be a real day, such as 2026-10-24.");
    }
    if (schedule.start <= now) {
      return base::unexpected("That time has passed.");
    }
  }
  if (!IsValid(schedule)) {
    return base::unexpected("The schedule card cannot make that schedule.");
  }
  card.request = std::string(trimmed);
  card.regular_updates =
      args.FindBool("regular_updates").value_or(card.regular_updates);
  return card;
}

}  // namespace uw
