// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/browser/agent/scheduled_chats.h"

#include <tuple>
#include <utility>
#include <vector>

#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/json/values_util.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/task/sequenced_task_runner.h"
#include "chrome/browser/profiles/keep_alive/profile_keep_alive_types.h"
#include "chrome/browser/profiles/keep_alive/scoped_profile_keep_alive.h"
#include "chrome/browser/profiles/profile.h"
#include "content/public/browser/device_service.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "uw/browser/agent/chat_alerts.h"
#include "uw/browser/agent/chat_file.h"
#include "uw/browser/agent/chat_notification.h"
#include "uw/browser/agent/run_window.h"
#include "uw/browser/ai_providers/catalog_store.h"
#include "uw/browser/ai_providers/models.h"
#include "uw/components/ai_providers/providers.h"

namespace uw {

namespace {

const void* const kUserDataKey = &kUserDataKey;

constexpr char kSchedulesFile[] = "schedules.json";

RunEnd EndOf(ChatRun::Outcome::Kind kind) {
  switch (kind) {
    case ChatRun::Outcome::Kind::kReported:
      return RunEnd::kReported;
    case ChatRun::Outcome::Kind::kFailed:
      return RunEnd::kFailed;
    case ChatRun::Outcome::Kind::kModelUnreachable:
      return RunEnd::kModelUnreachable;
    case ChatRun::Outcome::Kind::kStopped:
      return RunEnd::kStopped;
  }
}

std::string_view NameOf(RunEnd end) {
  switch (end) {
    case RunEnd::kReported:
      return "reported";
    case RunEnd::kFailed:
      return "failed";
    case RunEnd::kModelUnreachable:
      return "unreachable";
    case RunEnd::kStopped:
      return "stopped";
  }
}

ApprovalMode ModeFrom(std::string_view name) {
  if (name == "ask_first") {
    return ApprovalMode::kAskFirst;
  }
  return name == "full_auto" ? ApprovalMode::kFullAuto : ApprovalMode::kAuto;
}

// A run's steps as its line keeps them, to fold under it: each tool, what it
// was asked, and what went wrong, if anything.
// What a run's model requests used and cost, for its line.
base::DictValue RunUsageValue(const UsageTotal& total) {
  base::DictValue value = UsageToValue(total.usage);
  value.Set("cost", total.cost);
  value.Set("priced", total.priced);
  return value;
}

base::ListValue StepsValue(const std::vector<AgentLoop::Step>& steps) {
  base::ListValue list;
  for (const AgentLoop::Step& step : steps) {
    if (step.kind != AgentLoop::Step::Kind::kTool) {
      continue;
    }
    base::DictValue saved =
        base::DictValue().Set("tool", step.tool).Set("args", step.args.Clone());
    if (const std::string* error = step.result.FindString("error")) {
      saved.Set("error", *error);
    }
    list.Append(std::move(saved));
  }
  return list;
}

std::string_view NameOf(ChatTriggers::Stop why) {
  switch (why) {
    case ChatTriggers::Stop::kByUser:
      return "user";
    case ChatTriggers::Stop::kByAgent:
      return "agent";
    case ChatTriggers::Stop::kAfterADay:
      return "day";
    case ChatTriggers::Stop::kPrintedTooMuch:
      return "too_much";
    case ChatTriggers::Stop::kPaused:
      return "paused";
  }
}

base::ListValue ValuesValue(const std::vector<TrackedValue>& values) {
  base::ListValue list;
  for (const TrackedValue& value : values) {
    list.Append(TrackedValueToValue(value));
  }
  return list;
}

std::optional<base::DictValue> ReadSchedules(const base::FilePath& path) {
  std::string json;
  if (!base::ReadFileToString(path, &json)) {
    return std::nullopt;
  }
  return base::JSONReader::ReadDict(json, base::JSON_PARSE_RFC);
}

// Adds `lines` to the end of the saved chat in `file`, when there is one.
void AppendToChat(const base::FilePath& file, base::ListValue lines) {
  std::string json;
  if (!base::ReadFileToString(file, &json)) {
    return;
  }
  std::optional<base::DictValue> chat =
      base::JSONReader::ReadDict(json, base::JSON_PARSE_RFC);
  if (!chat) {
    return;
  }
  base::ListValue* log = chat->EnsureList("log");
  for (base::Value& line : lines) {
    log->Append(std::move(line));
  }
  if (std::optional<std::string> out = base::WriteJson(*chat)) {
    base::ImportantFileWriter::WriteFileAtomically(file, *out);
  }
}

}  // namespace

ScheduledChats::Run::Run() = default;
ScheduledChats::Run::Run(Run&&) = default;
ScheduledChats::Run& ScheduledChats::Run::operator=(Run&&) = default;
ScheduledChats::Run::~Run() = default;

// static
ScheduledChats* ScheduledChats::For(Profile* profile) {
  if (!profile || profile->IsOffTheRecord()) {
    return nullptr;
  }
  auto* chats =
      static_cast<ScheduledChats*>(profile->GetUserData(kUserDataKey));
  if (!chats) {
    auto made = std::make_unique<ScheduledChats>(profile);
    chats = made.get();
    profile->SetUserData(kUserDataKey, std::move(made));
  }
  return chats;
}

ScheduledChats::ScheduledChats(Profile* profile)
    : profile_(profile),
      writer_(
          ChatFile::FolderFor(profile->GetPath()).AppendASCII(kSchedulesFile),
          ChatFile::Sequence()),
      // The scheduler is this object's own, so it never calls after it.
      scheduler_(
          base::BindRepeating(&ScheduledChats::OnDue, base::Unretained(this))) {
  profile_observation_.Observe(profile);
  if (ChatIndex* index = ChatIndex::For(profile)) {
    index_observation_.Observe(index);
  }
  mojo::Remote<device::mojom::TimeZoneMonitor> monitor;
  content::GetDeviceService().BindTimeZoneMonitor(
      monitor.BindNewPipeAndPassReceiver());
  monitor->AddClient(time_zone_receiver_.BindNewPipeAndPassRemote());
  ChatFile::Sequence()->PostTaskAndReplyWithResult(
      FROM_HERE, base::BindOnce(&ReadSchedules, writer_.path()),
      base::BindOnce(&ScheduledChats::OnLoaded, weak_factory_.GetWeakPtr()));
}

ScheduledChats::~ScheduledChats() = default;

const ScheduledChat* ScheduledChats::Find(std::string_view chat_id) const {
  const auto chat = chats_.find(chat_id);
  return chat == chats_.end() ? nullptr : &chat->second;
}

bool ScheduledChats::Running(std::string_view chat_id) const {
  return runs_.contains(chat_id);
}

bool ScheduledChats::Waiting(std::string_view chat_id) const {
  const auto run = runs_.find(chat_id);
  return run != runs_.end() && !run->second.approval.is_null();
}

const AgentTools::Approval* ScheduledChats::WaitingStep(
    std::string_view chat_id) const {
  const auto run = runs_.find(chat_id);
  return run != runs_.end() && run->second.step ? &*run->second.step : nullptr;
}

const std::string* ScheduledChats::ChatWatching(const GURL& page) const {
  if (!page.is_valid()) {
    return nullptr;
  }
  const GURL address = WatchedAddress(page);
  for (const auto& [id, chat] : chats_) {
    if (!chat.page.empty() && WatchedAddress(GURL(chat.page)) == address) {
      return &id;
    }
  }
  return nullptr;
}

std::vector<std::string> ScheduledChats::ChatsWatching(const GURL& page) const {
  std::vector<std::string> watching;
  if (!page.is_valid()) {
    return watching;
  }
  const GURL address = WatchedAddress(page);
  for (const auto& [id, chat] : chats_) {
    if (!chat.page.empty() && WatchedAddress(GURL(chat.page)) == address) {
      watching.push_back(id);
    }
  }
  return watching;
}

std::optional<base::Time> ScheduledChats::NextRunOf(
    std::string_view chat_id) const {
  const ScheduledChat* chat = Find(chat_id);
  if (!chat || chat->paused) {
    return std::nullopt;
  }
  return NextRun(chat->schedule, chat->last);
}

void ScheduledChats::SetPage(const std::string& chat_id, Page* page) {
  if (page) {
    pages_[chat_id] = page;
  } else {
    pages_.erase(chat_id);
  }
}

void ScheduledChats::Save(const std::string& chat_id, ScheduledChat chat) {
  const auto saved = chats_.find(chat_id);
  const bool is_new = saved == chats_.end();
  if (!is_new) {
    // A changed schedule keeps what its runs found, and starts again.
    chat.recent = std::move(saved->second.recent);
    chat.last = saved->second.last;
  }
  const ChatTriggers::Trigger* running = triggers_.Find(chat_id);
  if (running && !chat.trigger.empty() && running->command == chat.trigger) {
    chat.trigger_wake = std::string(NameOf(running->wake));
    chat.trigger_folder = running->folder.value();
    triggers_.MakeLasting(chat_id);
  } else if (!is_new && !chat.trigger.empty() &&
             chat.trigger == saved->second.trigger) {
    chat.trigger_wake = saved->second.trigger_wake;
    chat.trigger_folder = saved->second.trigger_folder;
  } else {
    chat.trigger.clear();
    if (running && running->lasting) {
      triggers_.End(chat_id);
    }
  }
  chat.paused = false;
  chat.failures = 0;
  chat.failing = false;
  chat.pause_reason.clear();
  const base::Time now = base::Time::Now();
  if (is_new) {
    chat.last = now;
  }
  last_model_ =
      ModelChoice{chat.provider_id, chat.model, std::nullopt, chat.effort};
  chats_[chat_id] = std::move(chat);
  Persist();
  Arm(chat_id);
  StartLastingTrigger(chat_id);
  UpdateKeepAlive();
  if (is_new) {
    StartRun(chat_id, now);
  }
  Changed(chat_id);
}

base::expected<void, std::string> ScheduledChats::StartTrigger(
    const std::string& chat_id,
    ChatTriggers::Trigger trigger) {
  if (ending_) {
    return base::unexpected("uw is closing.");
  }
  trigger.lasting = false;
  return triggers_.Start(chat_id, std::move(trigger));
}

bool ScheduledChats::StopTrigger(const std::string& chat_id, bool by_agent) {
  const ChatTriggers::Trigger* trigger = triggers_.Find(chat_id);
  if (!trigger || trigger->lasting) {
    return false;
  }
  triggers_.StopTrigger(chat_id, by_agent ? ChatTriggers::Stop::kByAgent
                                          : ChatTriggers::Stop::kByUser);
  return true;
}

std::string ScheduledChats::Watch(ScheduledChat chat) {
  const std::string id = ChatFile::NewChatId();
  const base::Time now = base::Time::Now();
  ChatFile(profile_->GetPath(), id)
      .Save(base::DictValue()
                .Set("version", 1)
                .Set("title", chat.request)
                .Set("updated", base::TimeToValue(now))
                .Set("log",
                     base::ListValue().Append(base::DictValue()
                                                  .Set("kind", "user")
                                                  .Set("text", chat.request))));
  if (ChatIndex* index = ChatIndex::For(profile_)) {
    index->Update({id, chat.request, now, ChatIndex::State::kDone});
  }
  Save(id, std::move(chat));
  return id;
}

void ScheduledChats::Remove(const std::string& chat_id) {
  if (chats_.erase(chat_id) == 0) {
    return;
  }
  scheduler_.Remove(chat_id);
  runs_.erase(chat_id);
  if (const ChatTriggers::Trigger* trigger = triggers_.Find(chat_id);
      trigger && trigger->lasting) {
    triggers_.End(chat_id);
  }
  Persist();
  UpdateKeepAlive();
  Changed(chat_id);
}

void ScheduledChats::SetPaused(const std::string& chat_id, bool paused) {
  const auto chat = chats_.find(chat_id);
  if (chat == chats_.end() || chat->second.paused == paused) {
    return;
  }
  chat->second.paused = paused;
  if (!paused) {
    chat->second.last = base::Time::Now();
    chat->second.failures = 0;
    chat->second.failing = false;
    chat->second.pause_reason.clear();
  } else if (const ChatTriggers::Trigger* trigger = triggers_.Find(chat_id)) {
    if (trigger->lasting) {
      triggers_.End(chat_id);
    } else {
      triggers_.StopTrigger(chat_id, ChatTriggers::Stop::kPaused);
    }
  }
  Persist();
  Arm(chat_id);
  StartLastingTrigger(chat_id);
  UpdateKeepAlive();
  Changed(chat_id);
}

void ScheduledChats::SetAllPaused(bool paused) {
  std::vector<std::string> ids;
  for (const auto& [id, chat] : chats_) {
    ids.push_back(id);
  }
  for (const std::string& id : ids) {
    SetPaused(id, paused);
  }
  if (paused) {
    for (const std::string& id : triggers_.ChatIds()) {
      if (const ChatTriggers::Trigger* trigger = triggers_.Find(id);
          trigger && !trigger->lasting) {
        triggers_.StopTrigger(id, ChatTriggers::Stop::kPaused);
      }
    }
  }
}

void ScheduledChats::SetMuted(const std::string& chat_id, bool muted) {
  const auto chat = chats_.find(chat_id);
  if (chat == chats_.end() || chat->second.muted == muted) {
    return;
  }
  chat->second.muted = muted;
  Persist();
  Changed(chat_id);
}

void ScheduledChats::AnswerApproval(const std::string& chat_id,
                                    AgentLoop::ApprovalAnswer answer) {
  const auto run = runs_.find(chat_id);
  if (run == runs_.end() || run->second.approval.is_null()) {
    return;
  }
  run->second.step.reset();
  if (ChatAlerts* alerts = ChatAlerts::For(profile_)) {
    alerts->RemoveWaiting(chat_id);
  }
  std::move(run->second.approval).Run(std::move(answer));
  Changed(chat_id);
}

void ScheduledChats::Stop(const std::string& chat_id) {
  const auto run = runs_.find(chat_id);
  if (run != runs_.end() && run->second.run) {
    run->second.run->Stop();
  }
}

void ScheduledChats::CheckNow(const std::string& chat_id) {
  const auto chat = chats_.find(chat_id);
  if (ending_ || chat == chats_.end() || runs_.contains(chat_id)) {
    return;
  }
  const base::Time now = base::Time::Now();
  chat->second.last = now;
  Arm(chat_id);
  Persist();
  StartRun(chat_id, now);
}

void ScheduledChats::BringWaitingTab(const std::string& chat_id,
                                     tabs::TabInterface& chat_tab) {
  const auto run = runs_.find(chat_id);
  if (run == runs_.end() || !run->second.run || !run->second.step) {
    return;
  }
  if (tabs::TabInterface* tab =
          run->second.run->TabNumbered(run->second.step->tab)) {
    BringRunTab(*tab, chat_tab);
  }
}

void ScheduledChats::AddObserver(Observer* observer) {
  observers_.AddObserver(observer);
}

void ScheduledChats::RemoveObserver(Observer* observer) {
  observers_.RemoveObserver(observer);
}

void ScheduledChats::OnChatsChanged() {
  const ChatIndex* index = index_observation_.GetSource();
  std::vector<std::string> deleted;
  for (const auto& [id, chat] : chats_) {
    if (index->WasDeleted(id)) {
      deleted.push_back(id);
    }
  }
  for (const std::string& id : deleted) {
    Remove(id);
  }
  for (const std::string& id : triggers_.ChatIds()) {
    if (index->WasDeleted(id)) {
      triggers_.End(id);
    }
  }
  if (ChatAlerts* alerts = ChatAlerts::For(profile_)) {
    std::vector<std::string> gone;
    for (const ChatAlerts::Alert& alert : alerts->alerts()) {
      if (index->WasDeleted(alert.chat_id)) {
        gone.push_back(alert.chat_id);
      }
    }
    for (const std::string& id : gone) {
      alerts->RemoveChat(id);
    }
  }
}

void ScheduledChats::OnProfileWillBeDestroyed(Profile* profile) {
  // The chat index and the keep-alive's profile go away with the profile, in
  // no set order, so let go of them first.
  ending_ = true;
  // A run's step can't be approved after uw quits, so its notification goes
  // rather than stay.
  for (const auto& [chat_id, run] : runs_) {
    if (run.step) {
      CloseChatNotification(profile_, chat_id);
    }
  }
  runs_.clear();
  triggers_.EndAll();
  keep_alive_.reset();
  index_observation_.Reset();
  time_zone_receiver_.reset();
  profile_observation_.Reset();
}

void ScheduledChats::OnTimeZoneChange(const std::string& zone_id) {
  scheduler_.TimeZoneChanged();
}

void ScheduledChats::OnLoaded(std::optional<base::DictValue> saved) {
  const base::DictValue* chats = saved ? saved->FindDict("chats") : nullptr;
  if (const base::DictValue* model =
          saved ? saved->FindDict("last_model") : nullptr) {
    const std::string* provider = model->FindString("provider_id");
    const std::string* name = model->FindString("model");
    const std::string* effort = model->FindString("effort");
    if (provider && name) {
      last_model_ = ModelChoice{*provider, *name, std::nullopt,
                                effort ? *effort : std::string()};
    }
  }
  if (chats) {
    for (const auto [id, value] : *chats) {
      if (!ChatFile::IsChatId(id) || !value.is_dict() || chats_.contains(id)) {
        continue;
      }
      if (std::optional<ScheduledChat> chat =
              ScheduledChatFromValue(value.GetDict())) {
        chats_[id] = std::move(*chat);
        Arm(id);
      }
    }
  }
  loaded_ = true;
  for (const auto& [id, chat] : chats_) {
    StartLastingTrigger(id);
  }
  UpdateKeepAlive();
  for (const auto& [id, chat] : chats_) {
    Changed(id);
  }
}

void ScheduledChats::Arm(const std::string& chat_id) {
  const ScheduledChat& chat = chats_.at(chat_id);
  if (chat.paused) {
    scheduler_.Remove(chat_id);
  } else {
    scheduler_.Set(chat_id, chat.schedule, chat.last);
  }
}

void ScheduledChats::UpdateKeepAlive() {
  if (ending_) {
    return;
  }
  const bool any =
      !triggers_.empty() || std::ranges::any_of(chats_, [](const auto& entry) {
        return !entry.second.paused;
      });
  if (any && !keep_alive_) {
    keep_alive_ = ScopedProfileKeepAlive::TryAcquire(
        profile_, ProfileKeepAliveOrigin::kUwScheduledChats);
  } else if (!any) {
    keep_alive_.reset();
  }
}

void ScheduledChats::Persist() {
  if (!loaded_) {
    return;
  }
  base::DictValue chats;
  for (const auto& [id, chat] : chats_) {
    chats.Set(id, ScheduledChatToValue(chat));
  }
  base::DictValue saved = base::DictValue().Set("chats", std::move(chats));
  if (last_model_) {
    saved.Set("last_model", base::DictValue()
                                .Set("provider_id", last_model_->provider_id)
                                .Set("model", last_model_->model)
                                .Set("effort", last_model_->effort));
  }
  if (std::optional<std::string> json = base::WriteJson(saved)) {
    writer_.WriteNow(std::move(*json));
  }
}

void ScheduledChats::Changed(const std::string& chat_id) {
  for (Observer& observer : observers_) {
    observer.OnScheduleChanged(chat_id);
  }
}

void ScheduledChats::OnDue(const std::string& chat_id, base::Time when) {
  const auto chat = chats_.find(chat_id);
  if (chat == chats_.end()) {
    return;
  }
  chat->second.last = when;
  Persist();
  // One run of a chat goes at a time. A run that waits for the user lets the
  // times it was due go by.
  if (const auto run = runs_.find(chat_id); run != runs_.end()) {
    if (!run->second.approval.is_null()) {
      ++run->second.skipped;
    }
    return;
  }
  StartRun(chat_id, when);
}

void ScheduledChats::StartRun(const std::string& chat_id,
                              base::Time when,
                              const ChatTriggers::Trigger* trigger,
                              std::string wake) {
  if (ending_) {
    return;
  }
  const ScheduledChat& chat = chats_.at(chat_id);
  Run& run = runs_[chat_id] = Run();
  if (trigger) {
    run.trigger = trigger->command;
    run.wake = std::move(wake);
  }
  Changed(chat_id);
  const std::optional<Provider> provider =
      FindProvider(*profile_->GetPrefs(), chat.provider_id);
  if (!provider) {
    OnFormat(chat_id, when, std::nullopt);
    return;
  }
  CatalogStore::For(profile_)->FormatOf(
      *provider, chat.model,
      base::BindOnce(&ScheduledChats::OnFormat, weak_factory_.GetWeakPtr(),
                     chat_id, when));
}

void ScheduledChats::OnFormat(const std::string& chat_id,
                              base::Time when,
                              std::optional<ProviderFormat> format) {
  const auto chat = chats_.find(chat_id);
  const auto run = runs_.find(chat_id);
  if (chat == chats_.end() || run == runs_.end()) {
    return;
  }
  std::optional<AgentModel> model =
      ModelFor(ModelChoice{chat->second.provider_id, chat->second.model, format,
                           chat->second.effort});
  if (!model) {
    ChatRun::Outcome outcome;
    outcome.kind = ChatRun::Outcome::Kind::kModelUnreachable;
    outcome.error = "The model this schedule runs on is not set up.";
    OnRunDone(chat_id, when, std::move(outcome));
    return;
  }
  std::string brief = RunBrief(chat->second.request, chat->second.page, when,
                               chat->second.recent, chat->second.tracked);
  if (!run->second.wake.empty()) {
    base::StrAppend(&brief, {"\n\nThis run is on a wake of the schedule's "
                             "trigger. ",
                             run->second.wake});
  }
  run->second.run = std::make_unique<ChatRun>(
      profile_, std::move(*model), ModeFrom(chat->second.approval_mode), brief,
      base::BindOnce(&ScheduledChats::OnRunDone, weak_factory_.GetWeakPtr(),
                     chat_id, when));
  run->second.run->SetApprover(base::BindRepeating(
      &ScheduledChats::OnApproval, weak_factory_.GetWeakPtr(), chat_id));
}

std::optional<AgentModel> ScheduledChats::ModelFor(const ModelChoice& choice) {
  return AgentModelFor(*profile_, choice);
}

void ScheduledChats::StartTriggerRun(const std::string& chat_id,
                                     ChatTriggers::Trigger trigger,
                                     TriggerWakeLines wake) {
  const base::Time when = base::Time::Now();
  runs_[chat_id] = Run();
  runs_[chat_id].trigger = trigger.command;
  Changed(chat_id);
  const std::optional<Provider> provider =
      FindProvider(*profile_->GetPrefs(), trigger.provider_id);
  if (!provider) {
    OnTriggerRunFormat(chat_id, std::move(trigger), std::move(wake), when,
                       std::nullopt);
    return;
  }
  const std::string model = trigger.model;
  CatalogStore::For(profile_)->FormatOf(
      *provider, model,
      base::BindOnce(&ScheduledChats::OnTriggerRunFormat,
                     weak_factory_.GetWeakPtr(), chat_id, std::move(trigger),
                     std::move(wake), when));
}

void ScheduledChats::OnTriggerRunFormat(const std::string& chat_id,
                                        ChatTriggers::Trigger trigger,
                                        TriggerWakeLines wake,
                                        base::Time when,
                                        std::optional<ProviderFormat> format) {
  const auto run = runs_.find(chat_id);
  if (run == runs_.end()) {
    return;
  }
  std::optional<AgentModel> model = ModelFor(
      ModelChoice{trigger.provider_id, trigger.model, format, trigger.effort});
  if (!model) {
    ChatRun::Outcome outcome;
    outcome.kind = ChatRun::Outcome::Kind::kModelUnreachable;
    outcome.error = "The model for this trigger's runs is not set up.";
    OnTriggerRunDone(chat_id, trigger.command, when, std::move(outcome));
    return;
  }
  run->second.run = std::make_unique<ChatRun>(
      profile_, std::move(*model), ModeFrom(trigger.approval_mode),
      TriggerRunBrief(trigger.command, trigger.request, wake, when),
      base::BindOnce(&ScheduledChats::OnTriggerRunDone,
                     weak_factory_.GetWeakPtr(), chat_id, trigger.command,
                     when));
  run->second.run->SetApprover(base::BindRepeating(
      &ScheduledChats::OnApproval, weak_factory_.GetWeakPtr(), chat_id));
}

void ScheduledChats::OnTriggerRunDone(const std::string& chat_id,
                                      std::string command,
                                      base::Time when,
                                      ChatRun::Outcome outcome) {
  runs_.erase(chat_id);
  const RunEnd end = EndOf(outcome.kind);
  const bool found = end == RunEnd::kReported && outcome.report.found;
  base::ListValue lines;
  lines.Append(base::DictValue()
                   .Set("kind", "run")
                   .Set("time", base::TimeToValue(when))
                   .Set("end", NameOf(end))
                   .Set("found", found)
                   .Set("text", end == RunEnd::kReported
                                    ? outcome.report.message
                                    : outcome.error)
                   .Set("values", ValuesValue(outcome.report.values))
                   .Set("sign_in", outcome.report.sign_in)
                   .Set("what", found ? "Trigger" : "")
                   .Set("counted", false)
                   .Set("paused", false)
                   .Set("steps", StepsValue(outcome.steps))
                   .Set("trigger", command));
  AddLines(chat_id, std::move(lines));
  // Only what is worth telling wakes the chat's model and the user.
  if (found) {
    Notify(chat_id, "Trigger", outcome.report.message);
    if (const auto page = pages_.find(chat_id); page != pages_.end()) {
      page->second->Tell(base::StrCat(
          {"A run looked at a wake of your trigger `", command,
           "` and found: ", outcome.report.message,
           "\nThe user did not write this message; uw sends it."}));
    }
  }
  UpdateKeepAlive();
  Changed(chat_id);
  triggers_.Retry(chat_id);
}

void ScheduledChats::StartLastingTrigger(const std::string& chat_id) {
  const ScheduledChat& chat = chats_.at(chat_id);
  if (chat.trigger.empty() || chat.paused || triggers_.Find(chat_id) ||
      ending_) {
    return;
  }
  ChatTriggers::Trigger trigger;
  trigger.command = chat.trigger;
  trigger.wake = TriggerWakeNamed(chat.trigger_wake);
  trigger.request = chat.request;
  trigger.folder = base::FilePath(chat.trigger_folder);
  trigger.lasting = true;
  std::ignore = triggers_.Start(chat_id, std::move(trigger));
}

void ScheduledChats::AddLines(const std::string& chat_id,
                              base::ListValue lines) {
  if (const auto page = pages_.find(chat_id); page != pages_.end()) {
    page->second->AddRunLines(std::move(lines));
    return;
  }
  ChatFile::Sequence()->PostTask(
      FROM_HERE,
      base::BindOnce(&AppendToChat,
                     ChatFile::FolderFor(profile_->GetPath())
                         .AppendASCII(base::StrCat({chat_id, ".json"})),
                     std::move(lines)));
}

bool ScheduledChats::OnWake(const std::string& chat_id,
                            const ChatTriggers::Trigger& trigger,
                            const TriggerWakeLines& wake,
                            bool as_run) {
  if (ending_) {
    return true;
  }
  if (!as_run) {
    const auto page = pages_.find(chat_id);
    base::DictValue line =
        base::DictValue()
            .Set("kind", "wake")
            .Set("time", base::TimeToValue(base::Time::Now()))
            .Set("command", trigger.command)
            .Set("ended", wake.exit_code.has_value())
            .Set("code", wake.exit_code.value_or(0))
            .Set("lines", static_cast<int>(wake.lines.size()) + wake.dropped);
    if (page != pages_.end() &&
        page->second->Wake(
            std::move(line),
            WakeMessage(trigger.command, trigger.request, wake))) {
      return true;
    }
  }
  // One run of a chat goes at a time; the lines wait for this one.
  if (runs_.contains(chat_id)) {
    return false;
  }
  if (trigger.lasting && chats_.contains(chat_id)) {
    StartRun(chat_id, base::Time::Now(), &trigger,
             DescribeWake(trigger.command, wake));
    return true;
  }
  StartTriggerRun(chat_id, trigger, wake);
  return true;
}

void ScheduledChats::OnTriggerStopped(const std::string& chat_id,
                                      const ChatTriggers::Trigger& trigger,
                                      ChatTriggers::Stop why) {
  base::ListValue lines;
  lines.Append(base::DictValue()
                   .Set("kind", "trigger_stopped")
                   .Set("time", base::TimeToValue(base::Time::Now()))
                   .Set("command", trigger.command)
                   .Set("why", NameOf(why)));
  AddLines(chat_id, std::move(lines));
}

void ScheduledChats::OnTriggersChanged(const std::string& chat_id) {
  UpdateKeepAlive();
  Changed(chat_id);
}

void ScheduledChats::OnApproval(
    const std::string& chat_id,
    AgentTools::Approval approval,
    base::OnceCallback<void(AgentLoop::ApprovalAnswer)> answer) {
  const auto run = runs_.find(chat_id);
  if (run == runs_.end()) {
    return;
  }
  run->second.approval = std::move(answer);
  const std::string action = approval.action;
  run->second.step = std::move(approval);
  Notify(chat_id, "Waits for your approval", action, /*waiting=*/true);
  Changed(chat_id);
}

void ScheduledChats::OnRunDone(const std::string& chat_id,
                               base::Time when,
                               ChatRun::Outcome outcome) {
  int skipped = 0;
  std::string trigger;
  if (const auto run = runs_.find(chat_id); run != runs_.end()) {
    skipped = run->second.skipped;
    trigger = std::move(run->second.trigger);
    runs_.erase(run);
  }
  if (ChatAlerts* alerts = ChatAlerts::For(profile_)) {
    alerts->RemoveWaiting(chat_id);
  }
  // Lines a trigger printed while this run went wake the chat again.
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(
                     [](base::WeakPtr<ScheduledChats> self, std::string id) {
                       if (self) {
                         self->triggers_.Retry(id);
                       }
                     },
                     weak_factory_.GetWeakPtr(), chat_id));
  const auto chat = chats_.find(chat_id);
  if (chat == chats_.end()) {
    return;
  }
  const RunEnd end = EndOf(outcome.kind);
  if (end == RunEnd::kReported) {
    NameValues(chat->second, outcome.report);
  }
  const RunEffect effect =
      AfterRun(chat->second, end, outcome.report, outcome.error, when);
  base::ListValue lines;
  if (skipped > 0) {
    lines.Append(
        base::DictValue().Set("kind", "run_skipped").Set("count", skipped));
  }
  lines.Append(base::DictValue()
                   .Set("kind", "run")
                   .Set("time", base::TimeToValue(when))
                   .Set("end", NameOf(end))
                   .Set("found", outcome.report.found)
                   .Set("text", end == RunEnd::kReported
                                    ? outcome.report.message
                                    : outcome.error)
                   .Set("values", ValuesValue(outcome.report.values))
                   .Set("sign_in", outcome.report.sign_in)
                   .Set("what", effect.what)
                   .Set("counted", effect.counted)
                   .Set("paused", effect.paused)
                   .Set("steps", StepsValue(outcome.steps))
                   .Set("trigger", trigger)
                   .Set("usage", RunUsageValue(outcome.usage)));
  AddLines(chat_id, std::move(lines));
  if (!effect.what.empty()) {
    Notify(chat_id, effect.what, effect.text);
  }
  if (chat->second.schedule.repeat == Schedule::Repeat::kOnce) {
    // A schedule that runs once leaves an ordinary chat.
    Remove(chat_id);
    return;
  }
  if (effect.paused) {
    Arm(chat_id);
  }
  Persist();
  UpdateKeepAlive();
  Changed(chat_id);
}

void ScheduledChats::Notify(const std::string& chat_id,
                            const std::string& what,
                            const std::string& text,
                            bool waiting) {
  // A chat without a schedule can still have a trigger's runs to tell of.
  const auto chat = chats_.find(chat_id);
  if (chat != chats_.end() && chat->second.muted) {
    return;
  }
  std::string title;
  if (const ChatIndex* index = index_observation_.GetSource()) {
    for (const ChatIndex::Entry& entry : index->Entries()) {
      if (entry.id == chat_id) {
        title = entry.title;
      }
    }
  }
  NotifyAboutChat(profile_, chat_id, title, what, text);
  if (ChatAlerts* alerts = ChatAlerts::For(profile_)) {
    alerts->Add(chat_id, what, text, waiting);
  }
}

}  // namespace uw
