// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#ifndef UW_COMPONENTS_AGENT_SCHEDULE_H_
#define UW_COMPONENTS_AGENT_SCHEDULE_H_

#include <optional>
#include <string>

#include "base/time/time.h"

namespace uw {

// When a scheduled chat runs, per docs/product.md, "Scheduled chats".
struct Schedule {
  enum class Repeat {
    // Every `hours` hours, counted from `start`.
    kEveryHours,
    // Every day at `hour`:`minute`.
    kDaily,
    // Monday to Friday at `hour`:`minute`.
    kWeekdays,
    // Every `weekday` at `hour`:`minute`.
    kWeekly,
    // Once, at `start`.
    kOnce,
  };

  Repeat repeat = Repeat::kDaily;
  int hours = 1;
  int hour = 9;
  int minute = 0;
  // 0 is Sunday, as in base::Time::Exploded.
  int weekday = 1;
  base::Time start;
  // An IANA time zone, such as "America/New_York", or empty to follow the
  // Mac's own zone wherever it goes.
  std::string time_zone;

  bool operator==(const Schedule&) const = default;
};

// Whether the schedule card could have made `schedule`: every 1, 2, 3, 4, 6,
// or 12 hours, a real wall time and weekday, and a time zone ICU knows.
bool IsValid(const Schedule& schedule);

// The first time `schedule` runs after `after`, or nothing when it never runs
// again. A wall time that a daylight-saving change skips runs at the first
// time after the gap; one that comes twice runs the first time.
std::optional<base::Time> NextRun(const Schedule& schedule, base::Time after);

// Whether `schedule` is due at `now`, given `last`, the time it last ran or
// was due. Any number of runs missed in between make one catch-up run.
bool IsDue(const Schedule& schedule, base::Time last, base::Time now);

}  // namespace uw

#endif  // UW_COMPONENTS_AGENT_SCHEDULE_H_
