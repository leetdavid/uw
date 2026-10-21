// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/browser/agent/command_check.h"

#include <utility>

#include "base/functional/bind.h"
#include "chrome/browser/profiles/profile.h"
#include "content/public/browser/storage_partition.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"
#include "uw/browser/ai_providers/api_key_store.h"

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

void Read(DecisionFormat format,
          const CommandCheckInput& input,
          base::OnceCallback<void(std::optional<CommandCheckAnswer>)> done,
          RemoteModelClient::Outcome outcome) {
  std::move(done).Run(
      outcome.ok ? ParseCommandCheckResponse(format, outcome.body, input)
                 : std::nullopt);
}

}  // namespace

std::unique_ptr<RemoteModelClient> SendCommandCheck(
    Profile& profile,
    const AiConnectionConfig& chat_model,
    const std::string& chat_key,
    const CommandCheckInput& input,
    base::OnceCallback<void(std::optional<CommandCheckAnswer>)> done) {
  const PrefService* prefs = profile.GetPrefs();
  scoped_refptr<network::SharedURLLoaderFactory> loader =
      profile.GetDefaultStoragePartition()
          ->GetURLLoaderFactoryForBrowserProcess();
  DecisionFormat format = DecisionFormat::kChat;
  std::unique_ptr<RemoteModelClient> client;
  const DecisionModelConfig decision = ReadDecisionModelConfig(prefs);
  const std::optional<std::string> decision_key =
      IsComplete(decision) ? KeyIn(decision.key_account) : std::nullopt;
  std::optional<std::string> body =
      decision_key
          ? BuildCommandCheckRequestBody(decision.format, decision.model, input)
          : std::nullopt;
  if (body) {
    format = decision.format;
    client = std::make_unique<RemoteModelClient>(
        loader, DecisionUrl(decision), *decision_key, std::move(*body),
        kProviderTimeout);
  } else {
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
    client = RemoteModelClient::ForLanguageModel(loader, language, *key, *body,
                                                 kProviderTimeout);
  }
  if (!client) {
    return nullptr;
  }
  client->Send(base::BindOnce(&Read, format, input, std::move(done)));
  return client;
}

}  // namespace uw
