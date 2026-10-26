// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/browser/agent/quick_check.h"

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
std::unique_ptr<QuickCheck> QuickCheck::Send(
    Profile& profile,
    const AiConnectionConfig& chat_model,
    const std::string& chat_key,
    BuildBody build,
    Done done) {
  const PrefService* prefs = profile.GetPrefs();
  auto check = base::WrapUnique(new QuickCheck(profile, std::move(done)));
  const DecisionModelConfig decision = ReadDecisionModelConfig(prefs);
  const std::optional<std::string> decision_key =
      IsComplete(decision) ? KeyIn(decision.key_account) : std::nullopt;
  std::optional<std::string> body =
      decision_key ? build.Run(decision.format, decision.model) : std::nullopt;
  if (body) {
    check->format_ = decision.format;
    check->model_name_ = decision.model;
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
  body = build.Run(DecisionFormat::kChat, language.model);
  if (!body || language.endpoint.empty()) {
    return nullptr;
  }
  check->language_ = std::move(language);
  check->key_ = std::move(*key);
  check->body_ = std::move(*body);
  // A check never waits for the catalog: without one it asks as before.
  check->SendLanguageCheck(CatalogStore::For(&profile)->Current());
  return check;
}

QuickCheck::QuickCheck(Profile& profile, Done done)
    : loader_(profile.GetDefaultStoragePartition()
                  ->GetURLLoaderFactoryForBrowserProcess()),
      done_(std::move(done)) {}

QuickCheck::~QuickCheck() = default;

void QuickCheck::SendLanguageCheck(const ModelCatalog* catalog) {
  Provider provider;
  provider.preset = language_.preset;
  provider.address = language_.endpoint;
  provider.format = language_.format;
  const ModelAbilities abilities = catalog
                                       ? catalog->For(provider, language_.model)
                                       : UnknownModelAbilities(provider);
  model_name_ = language_.model;
  price_ = abilities.price;
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

void QuickCheck::SendToLanguageModel(const AiConnectionConfig& model,
                                     const std::string& body,
                                     base::TimeDelta timeout) {
  Start(
      RemoteModelClient::ForLanguageModel(loader_, model, key_, body, timeout));
}

void QuickCheck::Start(std::unique_ptr<RemoteModelClient> client) {
  client_ = std::move(client);
  client_->Send(
      base::BindOnce(&QuickCheck::OnAnswer, weak_factory_.GetWeakPtr()));
}

void QuickCheck::OnAnswer(RemoteModelClient::Outcome outcome) {
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
  if (outcome.ok) {
    usage_ = UsageInAnswer(outcome.body);
  }
  if (!outcome.ok) {
    std::move(done_).Run(std::nullopt);
    return;
  }
  std::move(done_).Run(Answer{format_, std::move(outcome.body)});
}

}  // namespace uw
