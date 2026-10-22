// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/browser/agent/command_check.h"

#include <utility>

#include "base/functional/bind.h"
#include "base/memory/ptr_util.h"
#include "base/task/sequenced_task_runner.h"
#include "chrome/browser/profiles/profile.h"
#include "content/public/browser/storage_partition.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"
#include "uw/browser/ai_providers/api_key_store.h"
#include "uw/browser/ai_providers/catalog_store.h"
#include "uw/components/ai_providers/catalog.h"
#include "uw/components/ai_providers/formats.h"

namespace uw {
namespace {

// The key in `account`: empty when there is no account, nothing when the key
// is gone.
std::optional<std::string> KeyIn(const std::string& account) {
  if (account.empty()) {
    return std::string();
  }
  std::optional<std::string> stored = ApiKeyStore().GetKey(account);
  if (!stored || stored->empty()) {
    return std::nullopt;
  }
  return stored;
}

// How long a language model that may think first has to answer.
constexpr base::TimeDelta kThinkingTimeout = base::Seconds(90);

}  // namespace

// static
std::unique_ptr<CommandCheck> CommandCheck::Send(
    Profile& profile,
    const AiConnectionConfig& chat_model,
    const std::string& chat_key,
    const CommandCheckInput& input,
    Done done) {
  const PrefService* prefs = profile.GetPrefs();
  auto check =
      base::WrapUnique(new CommandCheck(profile, input, std::move(done)));
  const DecisionModelConfig decision = ReadDecisionModelConfig(prefs);
  const std::optional<std::string> decision_key =
      IsComplete(decision) ? KeyIn(decision.key_account) : std::nullopt;
  std::optional<std::string> body =
      decision_key
          ? BuildCommandCheckRequestBody(decision.format, decision.model, input)
          : std::nullopt;
  if (body) {
    check->format_ = decision.format;
    check->Start(std::make_unique<RemoteModelClient>(
        check->loader_, DecisionUrl(decision), *decision_key, std::move(*body),
        kProviderTimeout));
    return check;
  }
  AiConnectionConfig language = ReadAiConnectionConfig(prefs);
  std::optional<std::string> key =
      IsComplete(language) ? KeyIn(language.key_account) : std::nullopt;
  if (!key) {
    // The chat's effort is for its own work.
    language = chat_model;
    language.effort.clear();
    key = chat_key;
  }
  body = BuildCommandCheckRequestBody(DecisionFormat::kChat, language.model,
                                      input);
  if (!body || language.endpoint.empty()) {
    return nullptr;
  }
  check->language_ = std::move(language);
  check->key_ = std::move(*key);
  check->body_ = std::move(*body);
  CatalogStore::For(&profile)->Get(base::BindOnce(
      &CommandCheck::OnCatalog, check->weak_factory_.GetWeakPtr()));
  return check;
}

CommandCheck::CommandCheck(Profile& profile,
                           const CommandCheckInput& input,
                           Done done)
    : loader_(profile.GetDefaultStoragePartition()
                  ->GetURLLoaderFactoryForBrowserProcess()),
      input_(input),
      done_(std::move(done)) {}

CommandCheck::~CommandCheck() = default;

void CommandCheck::OnCatalog(const ModelCatalog* catalog) {
  Provider provider;
  provider.preset = language_.preset;
  provider.address = language_.endpoint;
  provider.format = language_.format;
  const ModelAbilities abilities = catalog
                                       ? catalog->For(provider, language_.model)
                                       : UnknownModelAbilities(provider);
  started_ = base::TimeTicks::Now();
  const std::optional<std::string> quiet =
      WithoutThinking(body_, language_.preset, language_.format, abilities);
  if (!quiet) {
    SendToLanguageModel(language_, body_, kThinkingTimeout);
    return;
  }
  AiConnectionConfig model = language_;
  model.effort.clear();
  thinking_off_ = true;
  SendToLanguageModel(model, *quiet, kProviderTimeout);
}

void CommandCheck::SendToLanguageModel(const AiConnectionConfig& model,
                                       const std::string& body,
                                       base::TimeDelta timeout) {
  Start(
      RemoteModelClient::ForLanguageModel(loader_, model, key_, body, timeout));
}

void CommandCheck::Start(std::unique_ptr<RemoteModelClient> client) {
  client_ = std::move(client);
  client_->Send(
      base::BindOnce(&CommandCheck::OnAnswer, weak_factory_.GetWeakPtr()));
}

void CommandCheck::OnAnswer(RemoteModelClient::Outcome outcome) {
  // A provider that rejects the switch is asked again without it, in what is
  // left of the time a thinking model gets.
  if (thinking_off_ && !outcome.ok && outcome.http_status >= 400 &&
      outcome.http_status < 500) {
    thinking_off_ = false;
    // The client that answered is still on the stack.
    base::SequencedTaskRunner::GetCurrentDefault()->DeleteSoon(
        FROM_HERE, std::move(client_));
    SendToLanguageModel(language_, body_,
                        kThinkingTimeout - (base::TimeTicks::Now() - started_));
    return;
  }
  std::move(done_).Run(
      outcome.ok ? ParseCommandCheckResponse(format_, outcome.body, input_)
                 : std::nullopt);
}

}  // namespace uw
