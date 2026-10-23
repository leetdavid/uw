// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/components/agent/scheduled_chat.h"

#include <algorithm>
#include <array>
#include <utility>

#include "base/containers/fixed_flat_set.h"
#include "base/json/values_util.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"

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

// Query parameters that only say where a visit came from. Any whose name
// starts with utm_ goes too.
constexpr auto kTrackingParameters = base::MakeFixedFlatSet<std::string_view>(
    {"_ga", "dclid", "fbclid", "gclid", "igshid", "mc_cid", "mc_eid", "msclkid",
     "ref", "ref_src", "yclid"});

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

// A value's name as runs are matched by it.
std::string NameKey(std::string_view name) {
  std::string key = base::ToLowerASCII(name);
  base::ReplaceChars(key, " -", "_", &key);
  return key;
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
         page == other.page && watch_for == other.watch_for &&
         regular_updates == other.regular_updates &&
         even_when_nothing_changes == other.even_when_nothing_changes &&
         provider_id == other.provider_id && model == other.model &&
         effort == other.effort && approval_mode == other.approval_mode &&
         paused == other.paused && muted == other.muted && last == other.last &&
         failures == other.failures && failing == other.failing &&
         last_failure == other.last_failure &&
         pause_reason == other.pause_reason &&
         same_runs(recent, other.recent) && tracked == other.tracked;
}

GURL WatchedAddress(const GURL& url) {
  std::vector<std::string_view> kept;
  for (std::string_view pair :
       base::SplitStringPiece(url.query(), "&", base::KEEP_WHITESPACE,
                              base::SPLIT_WANT_NONEMPTY)) {
    const std::string_view name = pair.substr(0, pair.find('='));
    if (!name.starts_with("utm_") && !kTrackingParameters.contains(name)) {
      kept.push_back(pair);
    }
  }
  const std::string query = base::JoinString(kept, "&");
  GURL::Replacements replacements;
  replacements.ClearRef();
  if (query.empty()) {
    replacements.ClearQuery();
  } else {
    replacements.SetQueryStr(query);
  }
  return url.ReplaceComponents(replacements);
}

void NameValues(ScheduledChat& chat, RunReport& report) {
  // Enough for every value of several reports.
  constexpr size_t kMaxTracked = 20;
  for (TrackedValue& value : report.values) {
    const auto known = std::ranges::find(
        chat.tracked, NameKey(value.name),
        [](const TrackedName& tracked) { return NameKey(tracked.name); });
    if (known == chat.tracked.end()) {
      if (chat.tracked.size() < kMaxTracked) {
        chat.tracked.push_back({value.name, value.unit});
      }
      continue;
    }
    value.name = known->name;
    if (value.unit.empty() ||
        base::EqualsCaseInsensitiveASCII(value.unit, known->unit)) {
      value.unit = known->unit;
    }
  }
}

base::DictValue ScheduledChatToValue(const ScheduledChat& chat) {
  std::string_view repeat;
  for (const auto& [kind, name] : kRepeatNames) {
    if (kind == chat.schedule.repeat) {
      repeat = name;
    }
  }
  base::ListValue tracked;
  for (const TrackedName& name : chat.tracked) {
    tracked.Append(
        base::DictValue().Set("name", name.name).Set("unit", name.unit));
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
      .Set("page", chat.page)
      .Set("watch_for", chat.watch_for)
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
      .Set("last_failure", chat.last_failure)
      .Set("pause_reason", chat.pause_reason)
      .Set("recent", std::move(recent))
      .Set("tracked", std::move(tracked));
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
  chat.page = StringOr(value, "page");
  chat.watch_for = StringOr(value, "watch_for");
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
  chat.last_failure = StringOr(value, "last_failure");
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
  if (const base::ListValue* tracked = value.FindList("tracked")) {
    for (const base::Value& name : *tracked) {
      const base::DictValue* dict = name.GetIfDict();
      if (dict && dict->FindString("name")) {
        chat.tracked.push_back(
            {*dict->FindString("name"), StringOr(*dict, "unit")});
      }
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
    chat.last_failure.clear();
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
    chat.last_failure = failure;
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
