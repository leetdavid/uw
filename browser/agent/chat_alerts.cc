// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/browser/agent/chat_alerts.h"

#include <algorithm>
#include <memory>
#include <optional>
#include <utility>

#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/json/values_util.h"
#include "base/task/sequenced_task_runner.h"
#include "base/uuid.h"
#include "chrome/browser/profiles/profile.h"
#include "uw/browser/agent/chat_file.h"

namespace uw {
namespace {

const void* const kUserDataKey = &kUserDataKey;
constexpr char kAlertsFile[] = "alerts.json";

// The saved alerts, newest first. A step that waited for approval when uw
// quit can no longer be answered, so its alert is left out.
std::vector<ChatAlerts::Alert> Load(const base::FilePath& path) {
  std::vector<ChatAlerts::Alert> alerts;
  std::string json;
  if (!base::ReadFileToString(path, &json)) {
    return alerts;
  }
  std::optional<base::DictValue> saved =
      base::JSONReader::ReadDict(json, base::JSON_PARSE_RFC);
  const base::ListValue* list = saved ? saved->FindList("alerts") : nullptr;
  if (!list) {
    return alerts;
  }
  for (const base::Value& value : *list) {
    const base::DictValue* alert = value.GetIfDict();
    const std::string* id = alert ? alert->FindString("id") : nullptr;
    const std::string* chat = alert ? alert->FindString("chat") : nullptr;
    const std::string* what = alert ? alert->FindString("what") : nullptr;
    const std::string* text = alert ? alert->FindString("text") : nullptr;
    const std::optional<base::Time> time =
        alert ? base::ValueToTime(alert->Find("time")) : std::nullopt;
    if (id && chat && ChatFile::IsChatId(*chat) && what && text && time &&
        !alert->FindBool("waiting").value_or(false)) {
      alerts.push_back({*id, *chat, *what, *text, *time,
                        alert->FindBool("seen").value_or(false)});
    }
  }
  return alerts;
}

}  // namespace

// static
ChatAlerts* ChatAlerts::For(Profile* profile) {
  if (!profile || profile->IsOffTheRecord()) {
    return nullptr;
  }
  auto* alerts = static_cast<ChatAlerts*>(profile->GetUserData(kUserDataKey));
  if (!alerts) {
    auto made = std::make_unique<ChatAlerts>(profile->GetPath());
    alerts = made.get();
    profile->SetUserData(kUserDataKey, std::move(made));
  }
  return alerts;
}

ChatAlerts::ChatAlerts(const base::FilePath& profile_path)
    : writer_(ChatFile::FolderFor(profile_path).AppendASCII(kAlertsFile),
              ChatFile::Sequence()) {
  ChatFile::Sequence()->PostTaskAndReplyWithResult(
      FROM_HERE, base::BindOnce(&Load, writer_.path()),
      base::BindOnce(&ChatAlerts::OnLoaded, weak_factory_.GetWeakPtr()));
}

ChatAlerts::~ChatAlerts() = default;

size_t ChatAlerts::Unseen() const {
  return static_cast<size_t>(std::ranges::count(alerts_, false, &Alert::seen));
}

void ChatAlerts::Add(const std::string& chat_id,
                     const std::string& what,
                     const std::string& text,
                     bool waiting) {
  alerts_.insert(alerts_.begin(),
                 {base::Uuid::GenerateRandomV4().AsLowercaseString(), chat_id,
                  what, text, base::Time::Now(), false, waiting});
  Changed();
}

void ChatAlerts::SeeAll() {
  if (Unseen() == 0) {
    return;
  }
  for (Alert& alert : alerts_) {
    alert.seen = true;
  }
  Changed();
}

void ChatAlerts::SeeNewest(std::string_view chat_id) {
  const auto newest = std::ranges::find(alerts_, chat_id, &Alert::chat_id);
  if (newest != alerts_.end() && !newest->seen) {
    newest->seen = true;
    Changed();
  }
}

void ChatAlerts::See(std::string_view id) {
  const auto alert = std::ranges::find(alerts_, id, &Alert::id);
  if (alert != alerts_.end() && !alert->seen) {
    alert->seen = true;
    Changed();
  }
}

void ChatAlerts::Clear() {
  if (!alerts_.empty()) {
    alerts_.clear();
    Changed();
  }
}

void ChatAlerts::RemoveChat(std::string_view chat_id) {
  if (std::erase_if(alerts_, [chat_id](const Alert& alert) {
        return alert.chat_id == chat_id;
      })) {
    Changed();
  }
}

void ChatAlerts::RemoveWaiting(std::string_view chat_id) {
  if (std::erase_if(alerts_, [chat_id](const Alert& alert) {
        return alert.waiting && alert.chat_id == chat_id;
      })) {
    Changed();
  }
}

void ChatAlerts::AddObserver(Observer* observer) {
  observers_.AddObserver(observer);
}

void ChatAlerts::RemoveObserver(Observer* observer) {
  observers_.RemoveObserver(observer);
}

void ChatAlerts::OnLoaded(std::vector<Alert> saved) {
  // Alerts added while the file loaded are newer than it.
  for (Alert& alert : saved) {
    alerts_.push_back(std::move(alert));
  }
  loaded_ = true;
  Changed();
}

void ChatAlerts::Changed() {
  if (loaded_) {
    base::ListValue list;
    for (const Alert& alert : alerts_) {
      base::DictValue saved = base::DictValue()
                                  .Set("id", alert.id)
                                  .Set("chat", alert.chat_id)
                                  .Set("what", alert.what)
                                  .Set("text", alert.text)
                                  .Set("time", base::TimeToValue(alert.time))
                                  .Set("seen", alert.seen);
      if (alert.waiting) {
        saved.Set("waiting", true);
      }
      list.Append(std::move(saved));
    }
    if (std::optional<std::string> json =
            base::WriteJson(base::DictValue().Set("alerts", std::move(list)))) {
      writer_.WriteNow(std::move(*json));
    }
  }
  for (Observer& observer : observers_) {
    observer.OnAlertsChanged();
  }
}

}  // namespace uw
