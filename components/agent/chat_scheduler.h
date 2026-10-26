// Copyright 2026 The uw Authors. All rights reserved.

#ifndef UW_COMPONENTS_AGENT_CHAT_SCHEDULER_H_
#define UW_COMPONENTS_AGENT_CHAT_SCHEDULER_H_

#include <map>
#include <string>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/time/time.h"
#include "base/timer/wall_clock_timer.h"
#include "uw/components/agent/schedule.h"

namespace base {
class Clock;
class TickClock;
}  // namespace base

namespace uw {

// Starts each scheduled chat's runs on time. One timer waits for the earliest
// next run, and it is checked again after the Mac sleeps. Runs missed while uw
// was quit or the Mac slept make one catch-up run as soon as the chat is set
// or the Mac wakes.
class ChatScheduler {
 public:
  // Called with a chat whose run is due, and the time it fired. That time is
  // the chat's new last run.
  using RunCallback =
      base::RepeatingCallback<void(const std::string& chat_id, base::Time)>;

  explicit ChatScheduler(RunCallback run,
                         const base::Clock* clock = nullptr,
                         const base::TickClock* tick_clock = nullptr);
  ChatScheduler(const ChatScheduler&) = delete;
  ChatScheduler& operator=(const ChatScheduler&) = delete;
  ~ChatScheduler();

  // Adds or replaces the schedule of `chat_id`, which last ran, or was due,
  // at `last`.
  void Set(const std::string& chat_id,
           const Schedule& schedule,
           base::Time last);
  void Remove(const std::string& chat_id);

  // Called when the Mac's time zone changes, since a schedule without its own
  // zone keeps its wall time in the new one.
  void TimeZoneChanged();

 private:
  struct Entry {
    Schedule schedule;
    base::Time last;
  };

  void Arm();
  void Fire();

  const RunCallback run_;
  const raw_ptr<const base::Clock> clock_;
  std::map<std::string, Entry> entries_;
  base::WallClockTimer timer_;
};

}  // namespace uw

#endif  // UW_COMPONENTS_AGENT_CHAT_SCHEDULER_H_
