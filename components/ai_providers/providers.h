// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#ifndef UW_COMPONENTS_AI_PROVIDERS_PROVIDERS_H_
#define UW_COMPONENTS_AI_PROVIDERS_PROVIDERS_H_

#include <optional>
#include <string>
#include <string_view>
#include <vector>

class PrefRegistrySimple;
class PrefService;

namespace uw {

// The providers the user saved on uw://providers, and the three model
// choices that pick from them. See docs/plans/ai-providers.md. Keys live in
// the Keychain under KeyAccountFor(id); only the account name lives here.
inline constexpr char kProvidersPref[] = "uw.providers.list";
inline constexpr char kLanguageChoicePref[] = "uw.providers.language_model";
inline constexpr char kDecisionChoicePref[] = "uw.providers.decision_model";
inline constexpr char kAgentChoicePref[] = "uw.providers.agent_model";

// How uw talks to a provider. Stored in prefs. See ADR 0007 and ADR 0003.
enum class ProviderFormat {
  kChatCompletions = 0,
  kAnthropicMessages = 1,
  kOpenAiResponses = 2,
  // These two serve decision models only.
  kOpenAiDecisions = 3,
  kSystemOne = 4,
};

// The Add provider menu. Every preset but kCustom fills in a name, an
// address, and a format. Stored in prefs.
enum class ProviderPreset {
  kCustom = 0,
  kOpenAi = 1,
  kAnthropic = 2,
  kOpenRouter = 3,
  kVercelAiGateway = 4,
  kOllama = 5,
  kJev = 6,
};

struct Provider {
  std::string id;
  ProviderPreset preset = ProviderPreset::kCustom;
  std::string name;
  // A base address such as https://api.openai.com/v1.
  std::string address;
  ProviderFormat format = ProviderFormat::kChatCompletions;
  // Empty when no key is saved, as for a local Ollama.
  std::string key_account;
};

enum class ModelSlot { kLanguage, kDecision, kAgent };

struct ModelChoice {
  std::string provider_id;
  std::string model;
  // Whether the model passed a test since it was picked. Only the language
  // and decision models are tested.
  bool tested = false;
  // One of the model's effort levels, or empty for the provider's default.
  std::string effort;
};

void RegisterProviderPrefs(PrefRegistrySimple* registry);

// A preset as the Add provider menu fills it in, with no id yet.
Provider PresetProvider(ProviderPreset preset);

// Skips entries it cannot read.
std::vector<Provider> ReadProviders(const PrefService& prefs);
std::optional<Provider> FindProvider(const PrefService& prefs,
                                     std::string_view id);
// Saves `provider` under a new id made from its preset, and returns the id.
std::string AddProvider(PrefService& prefs, Provider provider);
// Replaces the saved provider with the same id. Returns false when there is
// none.
bool UpdateProvider(PrefService& prefs, const Provider& provider);
// The choices that used it stay, and wait for the user to pick another model.
void RemoveProvider(PrefService& prefs, std::string_view id);

// The Keychain account for a provider's key.
std::string KeyAccountFor(std::string_view provider_id);

std::optional<ModelChoice> ReadChoice(const PrefService& prefs, ModelSlot slot);
// A new pick has not been tested yet.
void SetChoice(PrefService& prefs,
               ModelSlot slot,
               std::string_view provider_id,
               std::string_view model);
void ClearChoice(PrefService& prefs, ModelSlot slot);
// Sets the effort of the model the slot holds. Empty is the provider's
// default. Picking another model starts on the default again.
void SetEffort(PrefService& prefs, ModelSlot slot, std::string_view effort);
// Records a passed test of `tested`, unless the slot holds another model by
// now.
void MarkTested(PrefService& prefs, ModelSlot slot, const ModelChoice& tested);

// The format of the provider's language and agent models, or nullopt when it
// serves only decision models.
std::optional<ProviderFormat> LanguageFormatOf(const Provider& provider);
// The format of the provider's decision models, or nullopt when it has none.
std::optional<ProviderFormat> DecisionFormatOf(const Provider& provider);

}  // namespace uw

#endif  // UW_COMPONENTS_AI_PROVIDERS_PROVIDERS_H_
