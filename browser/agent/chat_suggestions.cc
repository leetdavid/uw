// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/browser/agent/chat_suggestions.h"

#include <optional>
#include <utility>

#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/json/values_util.h"
#include "base/strings/strcat.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/sequenced_task_runner.h"
#include "base/values.h"
#include "chrome/browser/history/history_service_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "components/history/core/browser/history_service.h"
#include "components/keyed_service/core/service_access_type.h"
#include "content/public/browser/storage_partition.h"
#include "uw/browser/agent/chat_file.h"
#include "uw/browser/agent/chat_index.h"
#include "uw/browser/ai_providers/api_key_store.h"
#include "uw/browser/ai_providers/models.h"
#include "uw/browser/tab_organization/ai_connection.h"
#include "uw/components/agent/suggestions.h"

namespace uw {
namespace {

constexpr char kUserDataKey[] = "uw_chat_suggestions";
constexpr char kFile[] = "suggestions.json";
// How long to wait after an attempt that gave nothing before trying again.
constexpr base::TimeDelta kRetryAfter = base::Hours(1);
// How far back the most-visited sites go.
constexpr size_t kSiteDays = 7;

}  // namespace

// static
ChatSuggestions* ChatSuggestions::For(Profile* profile) {
  if (!profile || profile->IsOffTheRecord()) {
    return nullptr;
  }
  auto* suggestions =
      static_cast<ChatSuggestions*>(profile->GetUserData(kUserDataKey));
  if (!suggestions) {
    auto made = std::make_unique<ChatSuggestions>(profile);
    suggestions = made.get();
    profile->SetUserData(kUserDataKey, std::move(made));
  }
  return suggestions;
}

ChatSuggestions::ChatSuggestions(Profile* profile)
    : profile_(profile),
      writer_(ChatFile::FolderFor(profile->GetPath()).AppendASCII(kFile),
              ChatFile::Sequence()) {
  ChatFile::Sequence()->PostTaskAndReplyWithResult(
      FROM_HERE, base::BindOnce(&ChatSuggestions::Load, writer_.path()),
      base::BindOnce(&ChatSuggestions::OnLoaded, weak_factory_.GetWeakPtr()));
}

ChatSuggestions::~ChatSuggestions() = default;

void ChatSuggestions::Get(Callback callback) {
  if (!loaded_) {
    waiting_.push_back(std::move(callback));
    return;
  }
  std::move(callback).Run(saved_.suggestions);
  RefreshIfDue();
}

// static
ChatSuggestions::Saved ChatSuggestions::Load(const base::FilePath& file) {
  Saved saved;
  std::string json;
  if (!base::ReadFileToString(file, &json)) {
    return saved;
  }
  std::optional<base::DictValue> dict =
      base::JSONReader::ReadDict(json, base::JSON_PARSE_RFC);
  if (!dict) {
    return saved;
  }
  if (const base::ListValue* list = dict->FindList("suggestions")) {
    for (const base::Value& item : *list) {
      if (item.is_string()) {
        saved.suggestions.push_back(item.GetString());
      }
    }
  }
  if (std::optional<base::Time> written =
          base::ValueToTime(dict->Find("written"))) {
    saved.written = *written;
  }
  return saved;
}

void ChatSuggestions::OnLoaded(Saved saved) {
  saved_ = std::move(saved);
  loaded_ = true;
  for (Callback& callback : std::exchange(waiting_, {})) {
    std::move(callback).Run(saved_.suggestions);
  }
  RefreshIfDue();
}

void ChatSuggestions::RefreshIfDue() {
  const base::Time now = base::Time::Now();
  if (refreshing_ || now - saved_.written < kLifetime ||
      now - tried_ < kRetryAfter) {
    return;
  }
  history::HistoryService* history = HistoryServiceFactory::GetForProfile(
      profile_, ServiceAccessType::EXPLICIT_ACCESS);
  if (!history) {
    return;
  }
  refreshing_ = true;
  history->QueryMostVisitedURLs(
      kSuggestionSites * 2,
      base::BindOnce(&ChatSuggestions::OnSites, weak_factory_.GetWeakPtr()),
      &history_tasks_, std::nullopt, kSiteDays);
}

void ChatSuggestions::OnSites(history::MostVisitedURLList visited) {
  std::vector<VisitedSite> sites;
  for (const history::MostVisitedURL& site : visited) {
    if (sites.size() < kSuggestionSites && site.url.SchemeIsHTTPOrHTTPS()) {
      sites.push_back({base::UTF16ToUTF8(site.title),
                       base::StrCat({site.url.host(), site.url.path()})});
    }
  }
  std::vector<std::string> chats;
  if (ChatIndex* index = ChatIndex::For(profile_)) {
    for (const ChatIndex::Entry& entry : index->Entries()) {
      if (chats.size() < kSuggestionChats && !entry.title.empty()) {
        chats.push_back(entry.title);
      }
    }
  }
  const AiConnectionConfig language =
      ReadAiConnectionConfig(profile_->GetPrefs());
  std::optional<std::string> body =
      IsComplete(language)
          ? BuildSuggestionsRequestBody(language.model, chats, sites)
          : std::nullopt;
  std::string key;
  if (body && !language.key_account.empty()) {
    key = ApiKeyStore().GetKey(language.key_account).value_or(std::string());
  }
  if (!body || (!language.key_account.empty() && key.empty())) {
    refreshing_ = false;
    return;
  }
  tried_ = base::Time::Now();
  client_ = RemoteModelClient::ForLanguageModel(
      profile_->GetDefaultStoragePartition()
          ->GetURLLoaderFactoryForBrowserProcess(),
      language, std::move(key), *body, kProviderTimeout);
  client_->Send(
      base::BindOnce(&ChatSuggestions::OnAnswer, weak_factory_.GetWeakPtr()));
}

void ChatSuggestions::OnAnswer(RemoteModelClient::Outcome outcome) {
  // The client stays until the next request: it may still be on the stack.
  refreshing_ = false;
  std::optional<std::vector<std::string>> suggestions =
      outcome.ok ? ParseSuggestionsResponse(outcome.body) : std::nullopt;
  if (!suggestions) {
    return;
  }
  saved_ = {std::move(*suggestions), base::Time::Now()};
  base::ListValue list;
  for (const std::string& suggestion : saved_.suggestions) {
    list.Append(suggestion);
  }
  std::optional<std::string> json =
      base::WriteJson(base::DictValue()
                          .Set("suggestions", std::move(list))
                          .Set("written", base::TimeToValue(saved_.written)));
  if (!json) {
    return;
  }
  // A profile that has never saved a chat has no folder yet.
  ChatFile::Sequence()->PostTask(
      FROM_HERE, base::BindOnce(base::IgnoreResult(&base::CreateDirectory),
                                writer_.path().DirName()));
  writer_.WriteNow(std::move(*json));
}

}  // namespace uw
