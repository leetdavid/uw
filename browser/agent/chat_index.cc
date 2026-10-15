// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/browser/agent/chat_index.h"

#include <algorithm>
#include <memory>
#include <utility>

#include "base/files/file_enumerator.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/json/values_util.h"
#include "base/strings/strcat.h"
#include "base/task/sequenced_task_runner.h"
#include "chrome/browser/profiles/profile.h"
#include "uw/browser/agent/chat_file.h"

namespace uw {
namespace {

const void* const kUserDataKey = &kUserDataKey;
constexpr char kIndexFile[] = "index.json";

std::string_view NameOf(ChatIndex::State state) {
  switch (state) {
    case ChatIndex::State::kDone:
      return "done";
    case ChatIndex::State::kRunning:
      return "running";
    case ChatIndex::State::kStopped:
      return "stopped";
    case ChatIndex::State::kUnread:
      return "unread";
  }
}

// A chat that was running when the index was last written stopped when uw
// quit.
ChatIndex::State StateFrom(std::string_view name) {
  if (name == "running" || name == "stopped") {
    return ChatIndex::State::kStopped;
  }
  return name == "unread" ? ChatIndex::State::kUnread : ChatIndex::State::kDone;
}

// What the list needs from one chat file, for a profile with no index yet.
std::optional<ChatIndex::Entry> EntryFromChat(const base::FilePath& path) {
  std::string json;
  const std::string id = path.BaseName().RemoveExtension().MaybeAsASCII();
  if (!ChatFile::IsChatId(id) || !base::ReadFileToString(path, &json)) {
    return std::nullopt;
  }
  std::optional<base::DictValue> chat =
      base::JSONReader::ReadDict(json, base::JSON_PARSE_RFC);
  base::File::Info info;
  if (!chat || !base::GetFileInfo(path, &info)) {
    return std::nullopt;
  }
  const std::string* title = chat->FindString("title");
  const base::ListValue* log = chat->FindList("log");
  const base::DictValue* last =
      log && !log->empty() ? log->back().GetIfDict() : nullptr;
  const std::string* kind = last ? last->FindString("kind") : nullptr;
  ChatIndex::Entry entry;
  entry.id = id;
  entry.title = title ? *title : std::string();
  entry.updated = info.last_modified;
  entry.state =
      chat->FindBool("running").value_or(false) || (kind && *kind == "stopped")
          ? ChatIndex::State::kStopped
          : ChatIndex::State::kDone;
  return entry;
}

std::vector<ChatIndex::Entry> Load(const base::FilePath& folder) {
  std::vector<ChatIndex::Entry> entries;
  std::string json;
  if (base::ReadFileToString(folder.AppendASCII(kIndexFile), &json)) {
    std::optional<base::DictValue> index =
        base::JSONReader::ReadDict(json, base::JSON_PARSE_RFC);
    const base::ListValue* chats = index ? index->FindList("chats") : nullptr;
    if (!chats) {
      return entries;
    }
    for (const base::Value& value : *chats) {
      const base::DictValue* chat = value.GetIfDict();
      const std::string* id = chat ? chat->FindString("id") : nullptr;
      const std::string* title = chat ? chat->FindString("title") : nullptr;
      const std::string* state = chat ? chat->FindString("state") : nullptr;
      std::optional<base::Time> updated =
          chat ? base::ValueToTime(chat->Find("updated")) : std::nullopt;
      if (id && ChatFile::IsChatId(*id) && title && state && updated) {
        entries.push_back({*id, *title, *updated, StateFrom(*state)});
      }
    }
    return entries;
  }
  base::FileEnumerator files(folder, /*recursive=*/false,
                             base::FileEnumerator::FILES,
                             FILE_PATH_LITERAL("*.json"));
  for (base::FilePath path = files.Next(); !path.empty(); path = files.Next()) {
    if (std::optional<ChatIndex::Entry> entry = EntryFromChat(path)) {
      entries.push_back(std::move(*entry));
    }
  }
  return entries;
}

}  // namespace

// static
ChatIndex* ChatIndex::For(Profile* profile) {
  if (!profile || profile->IsOffTheRecord()) {
    return nullptr;
  }
  auto* index = static_cast<ChatIndex*>(profile->GetUserData(kUserDataKey));
  if (!index) {
    auto made = std::make_unique<ChatIndex>(profile->GetPath());
    index = made.get();
    profile->SetUserData(kUserDataKey, std::move(made));
  }
  return index;
}

ChatIndex::ChatIndex(const base::FilePath& profile_path)
    : folder_(ChatFile::FolderFor(profile_path)),
      writer_(folder_.AppendASCII(kIndexFile), ChatFile::Sequence()) {
  ChatFile::Sequence()->PostTaskAndReplyWithResult(
      FROM_HERE, base::BindOnce(&Load, folder_),
      base::BindOnce(&ChatIndex::OnLoaded, weak_factory_.GetWeakPtr()));
}

ChatIndex::~ChatIndex() = default;

std::vector<ChatIndex::Entry> ChatIndex::Entries() const {
  std::vector<Entry> entries;
  for (const auto& [id, entry] : entries_) {
    entries.push_back(entry);
  }
  std::ranges::sort(entries, std::ranges::greater(), &Entry::updated);
  return entries;
}

void ChatIndex::Update(Entry entry) {
  if (WasDeleted(entry.id)) {
    return;
  }
  const std::string id = entry.id;
  entries_[id] = std::move(entry);
  Save();
}

void ChatIndex::Delete(std::string_view id) {
  deleted_.insert(std::string(id));
  entries_.erase(std::string(id));
  ChatFile::Sequence()->PostTask(
      FROM_HERE,
      base::BindOnce(base::IgnoreResult(&base::DeleteFile),
                     folder_.AppendASCII(base::StrCat({id, ".json"}))));
  Save();
}

bool ChatIndex::WasDeleted(std::string_view id) const {
  return deleted_.contains(id);
}

void ChatIndex::AddObserver(Observer* observer) {
  observers_.AddObserver(observer);
}

void ChatIndex::RemoveObserver(Observer* observer) {
  observers_.RemoveObserver(observer);
}

void ChatIndex::OnLoaded(std::vector<Entry> entries) {
  // Chats saved while the index was loading are newer than the file.
  for (Entry& entry : entries) {
    const std::string id = entry.id;
    if (!WasDeleted(id)) {
      entries_.try_emplace(id, std::move(entry));
    }
  }
  loaded_ = true;
  Save();
}

void ChatIndex::Save() {
  if (loaded_) {
    base::ListValue chats;
    for (const Entry& entry : Entries()) {
      chats.Append(base::DictValue()
                       .Set("id", entry.id)
                       .Set("title", entry.title)
                       .Set("updated", base::TimeToValue(entry.updated))
                       .Set("state", NameOf(entry.state)));
    }
    if (std::optional<std::string> json =
            base::WriteJson(base::DictValue().Set("chats", std::move(chats)))) {
      writer_.WriteNow(std::move(*json));
    }
  }
  for (Observer& observer : observers_) {
    observer.OnChatsChanged();
  }
}

}  // namespace uw
