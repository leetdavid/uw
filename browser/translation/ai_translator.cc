// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/browser/translation/ai_translator.h"

#include <algorithm>
#include <utility>

#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/sequenced_task_runner.h"
#include "base/values.h"
#include "chrome/browser/profiles/profile.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/storage_partition.h"
#include "ui/base/l10n/l10n_util.h"
#include "uw/browser/ai_providers/api_key_store.h"
#include "uw/browser/ai_providers/models.h"
#include "uw/browser/translation/translation_prefs.h"
#include "uw/components/agent/agent_protocol.h"
#include "uw/components/ai_providers/providers.h"

namespace uw {

namespace {

constexpr char kInstructions[] =
    "You translate the text of a web page. The user sends a JSON array of "
    "strings, each a run of the page's text in which the page's elements are "
    "written as <a i=N>...</a>, with an empty <a i=N></a> for an element "
    "with no text of its own. Translate each string. Keep every tag, with "
    "its i, around the words that belong in it, in the order the "
    "translation needs, and change nothing else in the markup. Answer with "
    "only a JSON array of the same length, each item the translation of the "
    "string at the same place.";

std::string LanguageName(std::string_view code) {
  if (code.empty() || code == "auto" || code == "und") {
    return "the page's own language";
  }
  return base::UTF16ToUTF8(
      l10n_util::GetDisplayNameForLocale(std::string(code), "en", true));
}

std::string_view Unfenced(std::string_view text) {
  text = base::TrimWhitespaceASCII(text, base::TRIM_ALL);
  if (!text.starts_with("```")) {
    return text;
  }
  const size_t start = text.find('\n');
  const size_t end = text.rfind("```");
  if (start == std::string_view::npos || end <= start) {
    return text;
  }
  return base::TrimWhitespaceASCII(text.substr(start + 1, end - start - 1),
                                   base::TRIM_ALL);
}

}  // namespace

struct AiTranslator::Job {
  std::vector<std::string> segments;
  std::vector<std::string> translations;
  size_t pending = 0;
  std::optional<std::string> failure;
  ResultCallback callback;
  std::vector<std::unique_ptr<RemoteModelClient>> clients;
};

std::optional<ModelChoice> ReadTranslationModel(const PrefService& prefs) {
  const base::DictValue& dict = prefs.GetDict(kTranslationModelPref);
  const std::string* provider = dict.FindString("provider_id");
  const std::string* model = dict.FindString("model");
  if (!provider || !model || provider->empty() || model->empty()) {
    std::optional<ModelChoice> naming = ReadChoice(prefs, ModelSlot::kLanguage);
    if (naming) {
      naming->effort.clear();
    }
    return naming;
  }
  ModelChoice choice;
  choice.provider_id = *provider;
  choice.model = *model;
  choice.format = FormatFrom(dict.FindInt("format"));
  if (const std::string* effort = dict.FindString("effort")) {
    choice.effort = *effort;
  }
  return choice;
}

void SetTranslationModel(PrefService& prefs, const ModelChoice& choice) {
  base::DictValue dict;
  dict.Set("provider_id", choice.provider_id);
  dict.Set("model", choice.model);
  if (choice.format) {
    dict.Set("format", static_cast<int>(*choice.format));
  }
  dict.Set("effort", choice.effort);
  prefs.SetDict(kTranslationModelPref, std::move(dict));
}

std::string BuildTranslationRequestBody(
    std::string_view model,
    std::string_view source,
    std::string_view target,
    const std::vector<std::string>& segments) {
  base::ListValue texts;
  for (const std::string& segment : segments) {
    texts.Append(segment);
  }
  std::string instructions = std::string(kInstructions) + " Translate from " +
                             LanguageName(source) + " into " +
                             LanguageName(target) + ".";
  return base::WriteJson(
             base::DictValue()
                 .Set("model", model)
                 .Set("messages",
                      base::ListValue()
                          .Append(base::DictValue()
                                      .Set("role", "system")
                                      .Set("content", instructions))
                          .Append(UserMessage(
                              base::WriteJson(texts).value_or("[]")))))
      .value_or(std::string());
}

std::optional<std::vector<std::string>> ParseTranslationResponse(
    std::string_view body,
    size_t count) {
  const std::optional<ModelTurn> turn = ParseAgentResponse(body);
  if (!turn) {
    return std::nullopt;
  }
  std::optional<base::Value> answer =
      base::JSONReader::Read(Unfenced(turn->text), base::JSON_PARSE_RFC);
  if (!answer || !answer->is_list() || answer->GetList().size() != count) {
    return std::nullopt;
  }
  std::vector<std::string> translations;
  for (const base::Value& item : answer->GetList()) {
    if (!item.is_string()) {
      return std::nullopt;
    }
    translations.push_back(item.GetString());
  }
  return translations;
}

AiTranslator::AiTranslator(Profile* profile) : profile_(profile) {}

AiTranslator::~AiTranslator() = default;

void AiTranslator::Translate(std::vector<std::string> segments,
                             const std::string& source_language,
                             const std::string& target_language,
                             ResultCallback callback) {
  PrefService& prefs = *profile_->GetOriginalProfile()->GetPrefs();
  std::optional<ModelChoice> choice = ReadTranslationModel(prefs);
  const AiConnectionConfig config =
      choice ? ConfigForChoice(prefs, *choice) : AiConnectionConfig();
  if (!IsComplete(config)) {
    std::move(callback).Run(Result::Failed(std::string()));
    return;
  }
  std::string key;
  if (!config.key_account.empty()) {
    key = ApiKeyStore().GetKey(config.key_account).value_or(std::string());
  }
  if (segments.empty()) {
    std::move(callback).Run(Result());
    return;
  }

  auto job = std::make_unique<Job>();
  job->segments = std::move(segments);
  job->translations.resize(job->segments.size());
  job->callback = std::move(callback);
  Job* raw = job.get();
  jobs_.push_back(std::move(job));

  auto loader = profile_->GetDefaultStoragePartition()
                    ->GetURLLoaderFactoryForBrowserProcess();
  size_t first = 0;
  while (first < raw->segments.size()) {
    size_t chars = 0;
    size_t count = 0;
    while (first + count < raw->segments.size() &&
           (count == 0 ||
            chars + raw->segments[first + count].size() <= kMaxRequestChars)) {
      chars += raw->segments[first + count].size();
      ++count;
    }
    std::vector<std::string> chunk(raw->segments.begin() + first,
                                   raw->segments.begin() + first + count);
    std::unique_ptr<RemoteModelClient> client =
        RemoteModelClient::ForLanguageModel(
            loader, config, key,
            BuildTranslationRequestBody(config.model, source_language,
                                        target_language, chunk),
            kProviderTimeout);
    RemoteModelClient* client_ptr = client.get();
    raw->clients.push_back(std::move(client));
    ++raw->pending;
    client_ptr->Send(base::BindOnce(&AiTranslator::OnAnswer,
                                    weak_factory_.GetWeakPtr(), raw, first,
                                    count, client_ptr));
    first += count;
  }
}

void AiTranslator::OnAnswer(Job* job,
                            size_t first,
                            size_t count,
                            RemoteModelClient* client,
                            RemoteModelClient::Outcome outcome) {
  std::optional<std::vector<std::string>> translations =
      outcome.ok ? ParseTranslationResponse(outcome.body, count) : std::nullopt;
  if (translations) {
    for (size_t i = 0; i < count; ++i) {
      job->translations[first + i] = std::move((*translations)[i]);
    }
  } else if (!job->failure) {
    job->failure = outcome.ok ? std::string() : ProviderMessage(outcome.body);
  }
  if (--job->pending == 0) {
    // The client is still on the stack, so the job goes once it returns.
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE,
        base::BindOnce(&AiTranslator::Finish, weak_factory_.GetWeakPtr(), job));
  }
}

void AiTranslator::Finish(Job* job) {
  auto it = std::ranges::find_if(
      jobs_,
      [job](const std::unique_ptr<Job>& owned) { return owned.get() == job; });
  if (it == jobs_.end()) {
    return;
  }
  std::unique_ptr<Job> owned = std::move(*it);
  jobs_.erase(it);
  Result result;
  if (owned->failure) {
    result = Result::Failed(*owned->failure);
  } else {
    result.translations = std::move(owned->translations);
  }
  std::move(owned->callback).Run(std::move(result));
}

}  // namespace uw
