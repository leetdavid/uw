// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/browser/agent/chat_triggers.h"

#include <algorithm>
#include <utility>

#include "base/functional/bind.h"
#include "base/strings/strcat.h"
#include "uw/browser/agent/trigger_process.h"

namespace uw {
namespace {

// Lines that keep coming wake the chat this long after the first of them.
constexpr base::TimeDelta kLongestSettle = kTriggerLineGap * 5;
constexpr base::TimeDelta kMinute = base::Minutes(1);

TriggerLimits& Limits() {
  static TriggerLimits limits;
  return limits;
}

}  // namespace

ChatTriggers::Running::Running() = default;
ChatTriggers::Running::~Running() = default;

ChatTriggers::ChatTriggers(Delegate* delegate) : delegate_(delegate) {}

ChatTriggers::~ChatTriggers() = default;

// static
void ChatTriggers::SetLimitsForTesting(const TriggerLimits& limits) {
  Limits() = limits;
}

base::expected<void, std::string> ChatTriggers::Start(
    const std::string& chat_id,
    Trigger trigger) {
  if (const Trigger* running = Find(chat_id)) {
    return base::unexpected(
        base::StrCat({"This chat's trigger `", running->command,
                      "` still runs. Stop it first with stop_trigger."}));
  }
  auto running = std::make_unique<Running>();
  running->generation = next_generation_++;
  running->started = base::TimeTicks::Now();
  trigger.started = base::Time::Now();
  trigger.wakes = 0;
  running->trigger = std::move(trigger);
  const int generation = running->generation;
  if (!running->trigger.lasting) {
    running->life_timer.Start(
        FROM_HERE, Limits().life,
        base::BindOnce(&ChatTriggers::StopTrigger, base::Unretained(this),
                       chat_id, Stop::kAfterADay));
  }
  // The process reports on a later task, so the trigger is in place first.
  running->process = std::make_unique<TriggerProcess>(
      running->trigger.command, running->trigger.folder,
      base::BindRepeating(&ChatTriggers::OnLine, weak_factory_.GetWeakPtr(),
                          chat_id, generation),
      base::BindOnce(&ChatTriggers::OnEnd, weak_factory_.GetWeakPtr(), chat_id,
                     generation));
  running_[chat_id] = std::move(running);
  delegate_->OnTriggersChanged(chat_id);
  return base::ok();
}

void ChatTriggers::StopTrigger(const std::string& chat_id, Stop why) {
  auto found = running_.find(chat_id);
  if (found == running_.end()) {
    return;
  }
  std::unique_ptr<Running> stopped = std::move(found->second);
  running_.erase(found);
  delegate_->OnTriggerStopped(chat_id, stopped->trigger, why);
  delegate_->OnTriggersChanged(chat_id);
}

void ChatTriggers::End(const std::string& chat_id) {
  if (running_.erase(chat_id)) {
    delegate_->OnTriggersChanged(chat_id);
  }
}

void ChatTriggers::MakeLasting(const std::string& chat_id) {
  const auto found = running_.find(chat_id);
  if (found == running_.end()) {
    return;
  }
  found->second->trigger.lasting = true;
  found->second->life_timer.Stop();
  delegate_->OnTriggersChanged(chat_id);
}

void ChatTriggers::Retry(const std::string& chat_id) {
  const auto found = running_.find(chat_id);
  if (found != running_.end() && found->second->waiting) {
    found->second->waiting = false;
    Wake(chat_id);
  }
}

const ChatTriggers::Trigger* ChatTriggers::Find(
    std::string_view chat_id) const {
  const auto found = running_.find(chat_id);
  return found == running_.end() ? nullptr : &found->second->trigger;
}

std::vector<std::string> ChatTriggers::ChatIds() const {
  std::vector<std::string> ids;
  for (const auto& [id, running] : running_) {
    ids.push_back(id);
  }
  return ids;
}

ChatTriggers::Running* ChatTriggers::FindRunning(const std::string& chat_id,
                                                 int generation) {
  const auto found = running_.find(chat_id);
  return found == running_.end() || found->second->generation != generation
             ? nullptr
             : found->second.get();
}

void ChatTriggers::OnLine(const std::string& chat_id,
                          int generation,
                          std::string line) {
  Running* running = FindRunning(chat_id, generation);
  if (!running || running->ended) {
    return;
  }
  const base::TimeTicks now = base::TimeTicks::Now();
  const bool each_line = running->trigger.wake == TriggerWake::kLines;
  if (each_line) {
    if (now - running->minute_start >= kMinute) {
      running->minute_start = now;
      running->minute_lines = 0;
    }
    if (++running->minute_lines > kTriggerLinesPerMinute) {
      StopTrigger(chat_id, Stop::kPrintedTooMuch);
      return;
    }
  }
  if (running->pending.empty()) {
    running->pending_since = now;
  }
  AddWakeLine(running->pending, line);
  if (each_line && !running->pending.empty()) {
    ScheduleWake(chat_id, /*settle=*/true);
  }
}

void ChatTriggers::OnEnd(const std::string& chat_id, int generation, int code) {
  Running* running = FindRunning(chat_id, generation);
  if (!running) {
    return;
  }
  running->ended = true;
  running->pending.exit_code = code;
  ScheduleWake(chat_id, /*settle=*/false);
}

void ChatTriggers::ScheduleWake(const std::string& chat_id, bool settle) {
  Running& running = *running_.at(chat_id);
  if (running.waiting) {
    return;
  }
  const base::TimeTicks now = base::TimeTicks::Now();
  base::TimeTicks due = now;
  if (settle) {
    due =
        std::min(now + kTriggerLineGap, running.pending_since + kLongestSettle);
  }
  if (running.last_wake) {
    due = std::max(
        due, *running.last_wake + WakeGap(Limits(), running.trigger.lasting,
                                          now - running.started));
  }
  running.wake_timer.Start(
      FROM_HERE, std::max(due - now, base::TimeDelta()),
      base::BindOnce(&ChatTriggers::Wake, base::Unretained(this), chat_id));
}

void ChatTriggers::Wake(const std::string& chat_id) {
  const auto found = running_.find(chat_id);
  if (found == running_.end()) {
    return;
  }
  Running& running = *found->second;
  if (running.pending.empty() && !running.ended) {
    return;
  }
  const base::TimeTicks now = base::TimeTicks::Now();
  const bool as_run =
      WakeIsRun(Limits(), running.trigger.lasting, now - running.started);
  const int generation = running.generation;
  if (!delegate_->OnWake(chat_id, running.trigger, running.pending, as_run)) {
    if (Running* still = FindRunning(chat_id, generation)) {
      still->waiting = true;
    }
    return;
  }
  Running* woke = FindRunning(chat_id, generation);
  if (!woke) {
    return;
  }
  ++woke->trigger.wakes;
  woke->last_wake = now;
  woke->pending = TriggerWakeLines();
  if (woke->ended) {
    // A lasting trigger starts again when uw next launches, or once its
    // schedule card is saved.
    running_.erase(chat_id);
  }
  delegate_->OnTriggersChanged(chat_id);
}

}  // namespace uw
