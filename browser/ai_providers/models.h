// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#ifndef UW_BROWSER_AI_PROVIDERS_MODELS_H_
#define UW_BROWSER_AI_PROVIDERS_MODELS_H_

#include <optional>
#include <string>
#include <string_view>

#include "base/time/time.h"
#include "url/gurl.h"
#include "uw/components/ai_providers/providers.h"
#include "uw/components/tab_organization/ai_protocol.h"

class PrefRegistrySimple;
class PrefService;

namespace uw {

// The connections saved before uw://providers. MoveConnectionsToProviders
// reads them once and clears them.
inline constexpr char kOrganizationModePref[] = "uw.organization.mode";
inline constexpr char kOrganizationEndpointPref[] = "uw.organization.endpoint";
inline constexpr char kOrganizationModelPref[] = "uw.organization.model";
inline constexpr char kOrganizationKeyAccountPref[] =
    "uw.organization.key_account";
inline constexpr char kDecisionProviderPref[] =
    "uw.organization.decision.provider";
inline constexpr char kDecisionEndpointPref[] =
    "uw.organization.decision.endpoint";
inline constexpr char kDecisionModelPref[] = "uw.organization.decision.model";
inline constexpr char kDecisionKeyAccountPref[] =
    "uw.organization.decision.key_account";
inline constexpr char kAgentModePref[] = "uw.agent.mode";
inline constexpr char kAgentEndpointPref[] = "uw.agent.endpoint";
inline constexpr char kAgentModelPref[] = "uw.agent.model";
inline constexpr char kAgentKeyAccountPref[] = "uw.agent.key_account";

// What uw://ai/providers says once after the connections moved. Holds an
// UpgradeNotice.
inline constexpr char kUpgradeNoticePref[] = "uw.providers.upgrade_notice";

enum class UpgradeNotice {
  kNone = 0,
  // Each saved connection became a provider.
  kMoved = 1,
  // As kMoved, and connections with the same address and key became one.
  kMerged = 2,
  // A key could not be moved, so the old connections stay for the next
  // launch.
  kFailed = 3,
};

// How long uw waits for a provider's answer, while browsing and in a test
// alike. A local model can take this long to load, and a model that thinks
// first is slow to answer.
inline constexpr base::TimeDelta kProviderTimeout = base::Seconds(30);

// A language or agent model as a request needs it.
struct AiConnectionConfig {
  std::string endpoint;
  ProviderPreset preset = ProviderPreset::kCustom;
  ProviderFormat format = ProviderFormat::kChatCompletions;
  std::string model;
  // Empty for the provider's default.
  std::string effort;
  // Keychain account id holding the API key; empty when there is none.
  std::string key_account;
};

// The decision model as a request needs it.
struct DecisionModelConfig {
  DecisionFormat format = DecisionFormat::kDecisions;
  std::string endpoint;
  std::string model;
  std::string key_account;
};

// The providers, the model choices, and what the move from saved
// connections needs.
void RegisterModelPrefs(PrefRegistrySimple* registry);

// Turns the connections saved before uw://providers into providers, once.
// Connections with the same address and key become one provider. The old
// preferences and Keychain accounts go once every key has moved; until then
// they stay, and the next launch tries again.
void MoveConnectionsToProviders(PrefService* prefs);

// The models picked on uw://ai/providers. Empty when the slot holds no model,
// or its provider is gone or cannot serve it.
AiConnectionConfig ReadAiConnectionConfig(const PrefService* prefs);
// The model a new chat starts on: the agent model, or the language model
// until one is picked.
std::optional<ModelChoice> AgentChoice(const PrefService& prefs);
// The connection to `choice` as a language model. Empty when its provider is
// gone or cannot serve it.
AiConnectionConfig ConfigForChoice(const PrefService& prefs,
                                   const ModelChoice& choice);
DecisionModelConfig ReadDecisionModelConfig(const PrefService* prefs);

// Request URLs under a base endpoint like https://api.openai.com/v1. A
// language model takes requests at its format's own path.
GURL LanguageModelUrl(std::string_view endpoint, ProviderFormat format);
GURL ModelsUrl(std::string_view endpoint);
GURL DecisionUrl(const DecisionModelConfig& config);

// True when the connection has everything a request needs.
bool IsComplete(const AiConnectionConfig& config);
bool IsComplete(const DecisionModelConfig& config);

// The provider's own words about an error, when its answer has any. OpenAI
// and DeepSeek send {"error": {"message": ...}}, TypeSafe sends
// {"detail": {"message": ...}}, and Ollama sends {"error": "..."}.
std::string ProviderMessage(const std::string& body);

}  // namespace uw

#endif  // UW_BROWSER_AI_PROVIDERS_MODELS_H_
