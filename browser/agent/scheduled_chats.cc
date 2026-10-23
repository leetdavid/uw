// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/browser/agent/scheduled_chats.h"

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
#include "uw/browser/agent/chat_file.h"
#include "uw/browser/agent/chat_notification.h"
#include "uw/browser/agent/run_window.h"
#include "uw/browser/ai_providers/api_key_store.h"
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
  chat->Set("updated", base::TimeToValue(base::Time::Now()));
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
    if (chat.page.empty()) {
      chat.page = saved->second.page;
      if (chat.request == saved->second.request) {
        chat.watch_for = saved->second.watch_for;
      }
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
  UpdateKeepAlive();
  if (is_new) {
    StartRun(chat_id, now);
  }
  Changed(chat_id);
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
  }
  Persist();
  Arm(chat_id);
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
  std::move(run->second.approval).Run(std::move(answer));
  Changed(chat_id);
}

void ScheduledChats::Stop(const std::string& chat_id) {
  const auto run = runs_.find(chat_id);
  if (run != runs_.end() && run->second.run) {
    run->second.run->Stop();
  }
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
}

void ScheduledChats::OnProfileWillBeDestroyed(Profile* profile) {
  // The chat index and the keep-alive's profile go away with the profile, in
  // no set order, so let go of them first.
  ending_ = true;
  runs_.clear();
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
  const bool any = std::ranges::any_of(
      chats_, [](const auto& entry) { return !entry.second.paused; });
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

void ScheduledChats::StartRun(const std::string& chat_id, base::Time when) {
  if (ending_) {
    return;
  }
  const ScheduledChat& chat = chats_.at(chat_id);
  runs_[chat_id] = Run();
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
  AgentModel model;
  model.connection =
      ConfigForChoice(*profile_->GetPrefs(),
                      ModelChoice{chat->second.provider_id, chat->second.model,
                                  format, chat->second.effort});
  if (!IsComplete(model.connection)) {
    ChatRun::Outcome outcome;
    outcome.kind = ChatRun::Outcome::Kind::kModelUnreachable;
    outcome.error = "The model this schedule runs on is not set up.";
    OnRunDone(chat_id, when, std::move(outcome));
    return;
  }
  if (!model.connection.key_account.empty()) {
    model.api_key =
        ApiKeyStore().GetKey(model.connection.key_account).value_or("");
  }
  run->second.run = std::make_unique<ChatRun>(
      profile_, std::move(model), ModeFrom(chat->second.approval_mode),
      RunBrief(chat->second.request, chat->second.page, when,
               chat->second.recent, chat->second.tracked),
      base::BindOnce(&ScheduledChats::OnRunDone, weak_factory_.GetWeakPtr(),
                     chat_id, when));
  run->second.run->SetApprover(base::BindRepeating(
      &ScheduledChats::OnApproval, weak_factory_.GetWeakPtr(), chat_id));
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
  Notify(chat_id, "Waits for your approval", action);
  Changed(chat_id);
}

void ScheduledChats::OnRunDone(const std::string& chat_id,
                               base::Time when,
                               ChatRun::Outcome outcome) {
  int skipped = 0;
  if (const auto run = runs_.find(chat_id); run != runs_.end()) {
    skipped = run->second.skipped;
    runs_.erase(run);
  }
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
                   .Set("steps", StepsValue(outcome.steps)));
  if (const auto page = pages_.find(chat_id); page != pages_.end()) {
    page->second->AddRunLines(std::move(lines));
  } else {
    ChatFile::Sequence()->PostTask(
        FROM_HERE,
        base::BindOnce(&AppendToChat,
                       ChatFile::FolderFor(profile_->GetPath())
                           .AppendASCII(base::StrCat({chat_id, ".json"})),
                       std::move(lines)));
  }
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
                            const std::string& text) {
  const auto chat = chats_.find(chat_id);
  if (chat == chats_.end() || chat->second.muted) {
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
}

}  // namespace uw
