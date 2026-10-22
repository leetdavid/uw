// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/components/agent/scheduled_chat.h"

#include <algorithm>
#include <array>
#include <utility>

#include "base/json/values_util.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"

namespace uw {

namespace {

constexpr std::array<std::pair<Schedule::Repeat, std::string_view>, 5>
    kRepeatNames = {{
        {Schedule::Repeat::kEveryHours, "every_hours"},
        {Schedule::Repeat::kDaily, "daily"},
        {Schedule::Repeat::kWeekdays, "weekdays"},
        {Schedule::Repeat::kWeekly, "weekly"},
        {Schedule::Repeat::kOnce, "once"},
    }};

base::DictValue ReportToValue(const RunReport& report) {
  base::ListValue values;
  for (const TrackedValue& tracked : report.values) {
    values.Append(TrackedValueToValue(tracked));
  }
  return base::DictValue()
      .Set("found", report.found)
      .Set("message", report.message)
      .Set("values", std::move(values))
      .Set("sign_in", report.sign_in);
}

std::string StringOr(const base::DictValue& dict, std::string_view key) {
  const std::string* value = dict.FindString(key);
  return value ? *value : std::string();
}

}  // namespace

bool ScheduledChat::operator==(const ScheduledChat& other) const {
  const auto same_runs = [](const std::vector<PastRun>& a,
                            const std::vector<PastRun>& b) {
    return std::ranges::equal(a, b, [](const PastRun& x, const PastRun& y) {
      return x.time == y.time && x.report == y.report;
    });
  };
  return schedule == other.schedule && request == other.request &&
         regular_updates == other.regular_updates &&
         even_when_nothing_changes == other.even_when_nothing_changes &&
         provider_id == other.provider_id && model == other.model &&
         effort == other.effort && approval_mode == other.approval_mode &&
         paused == other.paused && muted == other.muted && last == other.last &&
         failures == other.failures && failing == other.failing &&
         pause_reason == other.pause_reason && same_runs(recent, other.recent);
}

base::DictValue ScheduledChatToValue(const ScheduledChat& chat) {
  std::string_view repeat;
  for (const auto& [kind, name] : kRepeatNames) {
    if (kind == chat.schedule.repeat) {
      repeat = name;
    }
  }
  base::ListValue recent;
  for (const PastRun& run : chat.recent) {
    recent.Append(base::DictValue()
                      .Set("time", base::TimeToValue(run.time))
                      .Set("report", ReportToValue(run.report)));
  }
  return base::DictValue()
      .Set("repeat", repeat)
      .Set("hours", chat.schedule.hours)
      .Set("hour", chat.schedule.hour)
      .Set("minute", chat.schedule.minute)
      .Set("weekday", chat.schedule.weekday)
      .Set("start", base::TimeToValue(chat.schedule.start))
      .Set("time_zone", chat.schedule.time_zone)
      .Set("request", chat.request)
      .Set("regular_updates", chat.regular_updates)
      .Set("even_when_nothing_changes", chat.even_when_nothing_changes)
      .Set("provider_id", chat.provider_id)
      .Set("model", chat.model)
      .Set("effort", chat.effort)
      .Set("approval_mode", chat.approval_mode)
      .Set("paused", chat.paused)
      .Set("muted", chat.muted)
      .Set("last", base::TimeToValue(chat.last))
      .Set("failures", chat.failures)
      .Set("failing", chat.failing)
      .Set("pause_reason", chat.pause_reason)
      .Set("recent", std::move(recent));
}

std::optional<ScheduledChat> ScheduledChatFromValue(
    const base::DictValue& value) {
  ScheduledChat chat;
  const std::string* repeat = value.FindString("repeat");
  bool known = false;
  for (const auto& [kind, name] : kRepeatNames) {
    if (repeat && *repeat == name) {
      chat.schedule.repeat = kind;
      known = true;
    }
  }
  if (!known) {
    return std::nullopt;
  }
  chat.schedule.hours = value.FindInt("hours").value_or(1);
  chat.schedule.hour = value.FindInt("hour").value_or(9);
  chat.schedule.minute = value.FindInt("minute").value_or(0);
  chat.schedule.weekday = value.FindInt("weekday").value_or(1);
  if (const base::Value* start = value.Find("start")) {
    chat.schedule.start = base::ValueToTime(*start).value_or(base::Time());
  }
  chat.schedule.time_zone = StringOr(value, "time_zone");
  chat.request = StringOr(value, "request");
  chat.regular_updates = value.FindBool("regular_updates").value_or(false);
  chat.even_when_nothing_changes =
      value.FindBool("even_when_nothing_changes").value_or(false);
  chat.provider_id = StringOr(value, "provider_id");
  chat.model = StringOr(value, "model");
  chat.effort = StringOr(value, "effort");
  chat.approval_mode = StringOr(value, "approval_mode");
  if (chat.approval_mode.empty()) {
    chat.approval_mode = "auto";
  }
  chat.paused = value.FindBool("paused").value_or(false);
  chat.muted = value.FindBool("muted").value_or(false);
  if (const base::Value* last = value.Find("last")) {
    chat.last = base::ValueToTime(*last).value_or(base::Time());
  }
  chat.failures = value.FindInt("failures").value_or(0);
  chat.failing = value.FindBool("failing").value_or(false);
  chat.pause_reason = StringOr(value, "pause_reason");
  if (const base::ListValue* recent = value.FindList("recent")) {
    for (const base::Value& run : *recent) {
      const base::DictValue* dict = run.GetIfDict();
      const base::Value* time = dict ? dict->Find("time") : nullptr;
      const base::DictValue* report = dict ? dict->FindDict("report") : nullptr;
      if (!time || !report) {
        continue;
      }
      base::expected<RunReport, std::string> parsed = ParseReport(*report);
      if (!parsed.has_value()) {
        continue;
      }
      chat.recent.push_back(
          {base::ValueToTime(*time).value_or(base::Time()), *parsed});
    }
  }
  if (chat.request.empty() || !IsValid(chat.schedule)) {
    return std::nullopt;
  }
  return chat;
}

RunEffect AfterRun(ScheduledChat& chat,
                   RunEnd end,
                   const RunReport& report,
                   std::string_view error,
                   base::Time when) {
  RunEffect effect;
  std::string failure;
  if (end == RunEnd::kStopped) {
    return effect;
  }
  if (end == RunEnd::kReported && report.sign_in.empty()) {
    chat.recent.push_back({when, report});
    if (chat.recent.size() > kPastRuns) {
      chat.recent.erase(chat.recent.begin());
    }
    chat.failures = 0;
    chat.failing = false;
    chat.pause_reason.clear();
    effect.text = report.message;
    if (chat.regular_updates) {
      effect.what = "Update";
    } else if (report.found) {
      effect.what = "Change alert";
    } else if (chat.even_when_nothing_changes) {
      effect.what = "No change";
    }
  } else {
    const bool first = !chat.failing;
    chat.failing = true;
    if (end == RunEnd::kModelUnreachable) {
      failure = std::string(error);
    } else {
      failure = end == RunEnd::kReported
                    ? base::StrCat({"Sign in required on ", report.sign_in})
                    : std::string(error);
      effect.counted = true;
      ++chat.failures;
    }
    effect.text = failure;
    if (first) {
      effect.what =
          end == RunEnd::kReported ? "Sign in required" : "Couldn't check";
    }
    if (chat.failures >= kFailuresBeforePause) {
      chat.paused = true;
      chat.pause_reason = failure;
      effect.paused = true;
      effect.what = "Paused";
      effect.text = base::StrCat({"Paused after ",
                                  base::NumberToString(kFailuresBeforePause),
                                  " failed runs: ", failure});
    }
  }
  if (chat.muted) {
    effect.what.clear();
  }
  return effect;
}

}  // namespace uw
