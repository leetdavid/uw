// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/components/agent/chat_scheduler.h"

#include <optional>
#include <vector>

#include "base/time/default_clock.h"

namespace uw {

ChatScheduler::ChatScheduler(RunCallback run,
                             const base::Clock* clock,
                             const base::TickClock* tick_clock)
    : run_(std::move(run)),
      clock_(clock ? clock : base::DefaultClock::GetInstance()),
      timer_(clock_, tick_clock) {}

ChatScheduler::~ChatScheduler() = default;

void ChatScheduler::Set(const std::string& chat_id,
                        const Schedule& schedule,
                        base::Time last) {
  entries_[chat_id] = {schedule, last};
  Arm();
}

void ChatScheduler::Remove(const std::string& chat_id) {
  entries_.erase(chat_id);
  Arm();
}

void ChatScheduler::TimeZoneChanged() {
  Arm();
}

void ChatScheduler::Arm() {
  std::optional<base::Time> earliest;
  for (const auto& [id, entry] : entries_) {
    std::optional<base::Time> next = NextRun(entry.schedule, entry.last);
    if (next && (!earliest || *next < *earliest)) {
      earliest = next;
    }
  }
  if (!earliest) {
    timer_.Stop();
    return;
  }
  timer_.Start(FROM_HERE, *earliest, this, &ChatScheduler::Fire);
}

void ChatScheduler::Fire() {
  const base::Time now = clock_->Now();
  std::vector<std::string> due;
  for (auto& [id, entry] : entries_) {
    if (IsDue(entry.schedule, entry.last, now)) {
      entry.last = now;
      due.push_back(id);
    }
  }
  Arm();
  for (const std::string& id : due) {
    run_.Run(id, now);
  }
}

}  // namespace uw
