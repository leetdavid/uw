// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/browser/ui/webui/uw_chat/schedule_card.h"

#include <memory>
#include <string>
#include <utility>

#include "base/json/json_writer.h"
#include "base/json/values_util.h"
#include "base/strings/strcat.h"
#include "base/strings/string_util.h"
#include "base/time/time.h"
#include "third_party/icu/source/i18n/unicode/timezone.h"
#include "uw/browser/agent/scheduled_chats.h"

namespace uw {
namespace {

constexpr std::pair<ApprovalMode, std::string_view> kModeNames[] = {
    {ApprovalMode::kAskFirst, "ask_first"},
    {ApprovalMode::kAuto, "auto"},
    {ApprovalMode::kFullAuto, "full_auto"}};

// The city of an IANA time zone, such as "Los Angeles", or of the Mac's own
// zone when `zone` is empty.
std::string ZoneName(const std::string& zone) {
  std::string id = zone;
  if (id.empty()) {
    icu::UnicodeString name;
    std::unique_ptr<icu::TimeZone>(icu::TimeZone::createDefault())->getID(name);
    name.toUTF8String(id);
  }
  const size_t slash = id.rfind('/');
  std::string city = slash == std::string::npos ? id : id.substr(slash + 1);
  base::ReplaceChars(city, "_", " ", &city);
  return city;
}

mojom::RunEnd RunEndNamed(const std::string* name) {
  if (!name || *name == "reported") {
    return mojom::RunEnd::kReported;
  }
  if (*name == "failed") {
    return mojom::RunEnd::kFailed;
  }
  return *name == "unreachable" ? mojom::RunEnd::kUnreachable
                                : mojom::RunEnd::kStopped;
}

}  // namespace

std::string_view NameOf(ApprovalMode mode) {
  for (const auto& [each, name] : kModeNames) {
    if (each == mode) {
      return name;
    }
  }
  return "auto";
}

ApprovalMode ModeNamed(std::string_view name) {
  for (const auto& [mode, each] : kModeNames) {
    if (each == name) {
      return mode;
    }
  }
  return ApprovalMode::kAuto;
}

ScheduledChat DefaultSchedule(const ScheduledChats* chats,
                              std::optional<ModelChoice> model,
                              ApprovalMode mode) {
  ScheduledChat chat;
  if (chats && chats->LastModel()) {
    model = chats->LastModel();
  }
  if (model) {
    chat.provider_id = model->provider_id;
    chat.model = model->model;
    chat.effort = model->effort;
  }
  chat.approval_mode = std::string(NameOf(mode));
  return chat;
}

mojom::ScheduleStatePtr ScheduleStateOf(const ScheduledChats* chats,
                                        std::string_view chat_id,
                                        const ScheduledChat& chat) {
  auto schedule = mojom::ChatSchedule::New();
  schedule->repeat = static_cast<mojom::ScheduleRepeat>(chat.schedule.repeat);
  schedule->hours = chat.schedule.hours;
  schedule->hour = chat.schedule.hour;
  schedule->minute = chat.schedule.minute;
  schedule->weekday = chat.schedule.weekday;
  schedule->once = chat.schedule.repeat == Schedule::Repeat::kOnce
                       ? chat.schedule.start.InMillisecondsFSinceUnixEpoch()
                       : 0;
  schedule->time_zone = chat.schedule.time_zone;
  schedule->request = chat.request;
  schedule->regular_updates = chat.regular_updates;
  schedule->even_when_nothing_changes = chat.even_when_nothing_changes;
  schedule->model =
      mojom::ChatModel::New(chat.provider_id, chat.model, chat.effort);
  schedule->mode =
      static_cast<mojom::ApprovalMode>(ModeNamed(chat.approval_mode));
  schedule->trigger = chat.trigger;

  auto state = mojom::ScheduleState::New();
  state->schedule = std::move(schedule);
  state->zone_name = ZoneName(chat.schedule.time_zone);
  if (chats && chats->Find(chat_id)) {
    if (const std::optional<base::Time> next = chats->NextRunOf(chat_id)) {
      state->next = next->InMillisecondsFSinceUnixEpoch();
    }
    state->running = chats->Running(chat_id);
    state->waiting = chats->Waiting(chat_id);
  }
  if (!chat.last.is_null()) {
    state->last = chat.last.InMillisecondsFSinceUnixEpoch();
  }
  if (!chat.recent.empty()) {
    const RunReport& report = chat.recent.back().report;
    state->last_text = report.message;
    for (const TrackedValue& value : report.values) {
      base::StrAppend(&state->last_text, {", ", DescribeValue(value)});
    }
  }
  state->page = chat.page;
  state->watch_for = chat.watch_for;
  state->paused = chat.paused;
  state->muted = chat.muted;
  state->pause_reason = chat.pause_reason;
  constexpr std::string_view kSignIn = "Sign in required on ";
  if (chat.failing && chat.last_failure.starts_with(kSignIn)) {
    state->sign_in = chat.last_failure.substr(kSignIn.size());
  }
  return state;
}

mojom::RunLinePtr RunLineFrom(const base::DictValue& line) {
  auto run = mojom::RunLine::New();
  run->time = base::ValueToTime(line.Find("time"))
                  .value_or(base::Time())
                  .InMillisecondsFSinceUnixEpoch();
  run->end = RunEndNamed(line.FindString("end"));
  run->found = line.FindBool("found").value_or(false);
  if (const std::string* what = line.FindString("what")) {
    run->what = *what;
  }
  // A run uw counted shows its cost, unless its model had no price.
  if (const base::DictValue* usage = line.FindDict("usage")) {
    const double cost = usage->FindDouble("cost").value_or(0);
    if (cost > 0 || usage->FindBool("priced").value_or(false)) {
      run->cost = cost;
    }
  }
  if (const base::ListValue* values = line.FindList("values")) {
    for (const base::Value& saved : *values) {
      const std::optional<TrackedValue> value =
          saved.is_dict() ? TrackedValueFromValue(saved.GetDict())
                          : std::nullopt;
      if (!value) {
        continue;
      }
      auto sent = mojom::TrackedValue::New();
      sent->name = value->name;
      sent->unit = value->unit;
      if (const double* number = std::get_if<double>(&value->value)) {
        sent->number = *number;
      } else if (const bool* yes = std::get_if<bool>(&value->value)) {
        sent->yes = *yes;
      } else {
        sent->text = std::get<std::string>(value->value);
      }
      run->values.push_back(std::move(sent));
    }
  }
  if (const base::ListValue* steps = line.FindList("steps")) {
    for (const base::Value& step : *steps) {
      const base::DictValue* saved = step.GetIfDict();
      const std::string* tool = saved ? saved->FindString("tool") : nullptr;
      if (!tool) {
        continue;
      }
      const base::DictValue* args = saved->FindDict("args");
      std::string shown = base::StrCat(
          {*tool, " ", args ? base::WriteJson(*args).value_or("{}") : "{}"});
      if (const std::string* error = saved->FindString("error")) {
        base::StrAppend(&shown, {": ", *error});
      }
      run->steps.push_back(std::move(shown));
    }
  }
  run->counted = line.FindBool("counted").value_or(false);
  run->paused = line.FindBool("paused").value_or(false);
  if (const std::string* trigger = line.FindString("trigger")) {
    run->trigger = *trigger;
  }
  return run;
}

std::optional<ScheduledChat> ScheduledChatFrom(
    const mojom::ChatSchedule& schedule) {
  ScheduledChat chat;
  chat.schedule.repeat = static_cast<Schedule::Repeat>(schedule.repeat);
  chat.schedule.hours = schedule.hours;
  chat.schedule.hour = schedule.hour;
  chat.schedule.minute = schedule.minute;
  chat.schedule.weekday = schedule.weekday;
  chat.schedule.time_zone = schedule.time_zone;
  chat.schedule.start =
      chat.schedule.repeat == Schedule::Repeat::kOnce
          ? base::Time::FromMillisecondsSinceUnixEpoch(schedule.once)
          : base::Time::Now();
  chat.request =
      std::string(base::TrimWhitespaceASCII(schedule.request, base::TRIM_ALL));
  chat.regular_updates = schedule.regular_updates;
  chat.even_when_nothing_changes = schedule.even_when_nothing_changes;
  chat.provider_id = schedule.model->provider_id;
  chat.model = schedule.model->model;
  chat.effort = schedule.model->effort;
  chat.approval_mode =
      std::string(NameOf(static_cast<ApprovalMode>(schedule.mode)));
  chat.trigger = schedule.trigger;
  if (chat.request.empty() || !IsValid(chat.schedule)) {
    return std::nullopt;
  }
  return chat;
}

}  // namespace uw
