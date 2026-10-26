// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/components/agent/schedule.h"

#include <algorithm>
#include <memory>

#include "third_party/icu/source/common/unicode/unistr.h"
#include "third_party/icu/source/i18n/unicode/gregocal.h"
#include "third_party/icu/source/i18n/unicode/timezone.h"

namespace uw {

namespace {

constexpr int kEveryHoursChoices[] = {1, 2, 3, 4, 6, 12};

// The zone `schedule` keeps its wall times in, or null for an unknown one.
std::unique_ptr<icu::TimeZone> ZoneOf(const Schedule& schedule) {
  if (schedule.time_zone.empty()) {
    return std::unique_ptr<icu::TimeZone>(icu::TimeZone::createDefault());
  }
  std::unique_ptr<icu::TimeZone> zone(icu::TimeZone::createTimeZone(
      icu::UnicodeString::fromUTF8(schedule.time_zone)));
  if (*zone == icu::TimeZone::getUnknown()) {
    return nullptr;
  }
  return zone;
}

bool RunsOn(const Schedule& schedule, int weekday) {
  switch (schedule.repeat) {
    case Schedule::Repeat::kWeekdays:
      return weekday >= 1 && weekday <= 5;
    case Schedule::Repeat::kWeekly:
      return weekday == schedule.weekday;
    default:
      return true;
  }
}

std::optional<base::Time> NextWallTime(const Schedule& schedule,
                                       base::Time after) {
  std::unique_ptr<icu::TimeZone> zone = ZoneOf(schedule);
  if (!zone) {
    return std::nullopt;
  }
  UErrorCode status = U_ZERO_ERROR;
  icu::GregorianCalendar calendar(zone.release(), status);
  if (U_FAILURE(status)) {
    return std::nullopt;
  }
  calendar.setSkippedWallTimeOption(UCAL_WALLTIME_NEXT_VALID);
  calendar.setRepeatedWallTimeOption(UCAL_WALLTIME_FIRST);
  calendar.setTime(after.InMillisecondsSinceUnixEpoch(), status);
  // A week and a day covers a weekly run due just before `after`.
  for (int day = 0; day < 9 && U_SUCCESS(status); ++day) {
    calendar.set(UCAL_HOUR_OF_DAY, schedule.hour);
    calendar.set(UCAL_MINUTE, schedule.minute);
    calendar.set(UCAL_SECOND, 0);
    calendar.set(UCAL_MILLISECOND, 0);
    const UDate run = calendar.getTime(status);
    const int weekday = calendar.get(UCAL_DAY_OF_WEEK, status) - UCAL_SUNDAY;
    if (U_SUCCESS(status) && run > after.InMillisecondsSinceUnixEpoch() &&
        RunsOn(schedule, weekday)) {
      return base::Time::FromMillisecondsSinceUnixEpoch(run);
    }
    calendar.add(UCAL_DATE, 1, status);
  }
  return std::nullopt;
}

}  // namespace

bool IsValid(const Schedule& schedule) {
  switch (schedule.repeat) {
    case Schedule::Repeat::kEveryHours:
      return std::ranges::contains(kEveryHoursChoices, schedule.hours) &&
             !schedule.start.is_null();
    case Schedule::Repeat::kOnce:
      return !schedule.start.is_null();
    case Schedule::Repeat::kWeekly:
      if (schedule.weekday < 0 || schedule.weekday > 6) {
        return false;
      }
      [[fallthrough]];
    case Schedule::Repeat::kDaily:
    case Schedule::Repeat::kWeekdays:
      return schedule.hour >= 0 && schedule.hour < 24 && schedule.minute >= 0 &&
             schedule.minute < 60 && ZoneOf(schedule) != nullptr;
  }
}

std::optional<base::Time> NextRun(const Schedule& schedule, base::Time after) {
  if (!IsValid(schedule)) {
    return std::nullopt;
  }
  switch (schedule.repeat) {
    case Schedule::Repeat::kEveryHours: {
      if (after < schedule.start) {
        return schedule.start;
      }
      const base::TimeDelta every = base::Hours(schedule.hours);
      return schedule.start +
             every * ((after - schedule.start).IntDiv(every) + 1);
    }
    case Schedule::Repeat::kOnce:
      if (after < schedule.start) {
        return schedule.start;
      }
      return std::nullopt;
    case Schedule::Repeat::kDaily:
    case Schedule::Repeat::kWeekdays:
    case Schedule::Repeat::kWeekly:
      return NextWallTime(schedule, after);
  }
}

bool IsDue(const Schedule& schedule, base::Time last, base::Time now) {
  std::optional<base::Time> next = NextRun(schedule, last);
  return next && *next <= now;
}

}  // namespace uw
