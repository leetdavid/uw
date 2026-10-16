// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/components/ai_providers/providers.h"

#include <algorithm>
#include <string>
#include <utility>

#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/values.h"
#include "components/prefs/pref_registry_simple.h"
#include "components/prefs/pref_service.h"
#include "components/prefs/scoped_user_pref_update.h"

namespace uw {
namespace {

constexpr char kId[] = "id";
constexpr char kPreset[] = "preset";
constexpr char kName[] = "name";
constexpr char kAddress[] = "address";
constexpr char kFormat[] = "format";
constexpr char kKeyAccount[] = "key_account";
constexpr char kProvider[] = "provider";
constexpr char kModel[] = "model";
constexpr char kEffort[] = "effort";

const char* ChoicePref(ModelSlot slot) {
  switch (slot) {
    case ModelSlot::kLanguage:
      return kLanguageChoicePref;
    case ModelSlot::kDecision:
      return kDecisionChoicePref;
    case ModelSlot::kAgent:
      return kAgentChoicePref;
  }
}

// The start of a preset's ids, as in uw://providers/openai.
const char* Slug(ProviderPreset preset) {
  switch (preset) {
    case ProviderPreset::kCustom:
      return "custom";
    case ProviderPreset::kOpenAi:
      return "openai";
    case ProviderPreset::kAnthropic:
      return "anthropic";
    case ProviderPreset::kOpenRouter:
      return "openrouter";
    case ProviderPreset::kVercelAiGateway:
      return "vercel";
    case ProviderPreset::kOllama:
      return "ollama";
    case ProviderPreset::kJev:
      return "jev";
  }
}

std::optional<ProviderPreset> PresetFrom(std::optional<int> value) {
  if (!value || *value < static_cast<int>(ProviderPreset::kCustom) ||
      *value > static_cast<int>(ProviderPreset::kJev)) {
    return std::nullopt;
  }
  return static_cast<ProviderPreset>(*value);
}

std::optional<ProviderFormat> FormatFrom(std::optional<int> value) {
  if (!value || *value < static_cast<int>(ProviderFormat::kChatCompletions) ||
      *value > static_cast<int>(ProviderFormat::kSystemOne)) {
    return std::nullopt;
  }
  return static_cast<ProviderFormat>(*value);
}

std::optional<Provider> ProviderFrom(const base::Value& entry) {
  const base::DictValue* dict = entry.GetIfDict();
  if (!dict) {
    return std::nullopt;
  }
  const std::string* id = dict->FindString(kId);
  const std::string* name = dict->FindString(kName);
  const std::string* address = dict->FindString(kAddress);
  const std::string* key_account = dict->FindString(kKeyAccount);
  const std::optional<ProviderPreset> preset =
      PresetFrom(dict->FindInt(kPreset));
  const std::optional<ProviderFormat> format =
      FormatFrom(dict->FindInt(kFormat));
  if (!id || id->empty() || !name || !address || !preset || !format) {
    return std::nullopt;
  }
  return Provider{*id,     *preset,
                  *name,   *address,
                  *format, key_account ? *key_account : std::string()};
}

base::DictValue DictFrom(const Provider& provider) {
  return base::DictValue()
      .Set(kId, provider.id)
      .Set(kPreset, static_cast<int>(provider.preset))
      .Set(kName, provider.name)
      .Set(kAddress, provider.address)
      .Set(kFormat, static_cast<int>(provider.format))
      .Set(kKeyAccount, provider.key_account);
}

}  // namespace

void RegisterProviderPrefs(PrefRegistrySimple* registry) {
  registry->RegisterListPref(kProvidersPref);
  for (ModelSlot slot :
       {ModelSlot::kLanguage, ModelSlot::kDecision, ModelSlot::kAgent}) {
    registry->RegisterDictionaryPref(ChoicePref(slot));
  }
}

Provider PresetProvider(ProviderPreset preset) {
  Provider provider;
  provider.preset = preset;
  switch (preset) {
    case ProviderPreset::kCustom:
      break;
    case ProviderPreset::kOpenAi:
      provider.name = "OpenAI";
      provider.address = "https://api.openai.com/v1";
      provider.format = ProviderFormat::kOpenAiResponses;
      break;
    case ProviderPreset::kAnthropic:
      provider.name = "Anthropic";
      provider.address = "https://api.anthropic.com/v1";
      provider.format = ProviderFormat::kAnthropicMessages;
      break;
    case ProviderPreset::kOpenRouter:
      provider.name = "OpenRouter";
      provider.address = "https://openrouter.ai/api/v1";
      break;
    case ProviderPreset::kVercelAiGateway:
      provider.name = "Vercel AI Gateway";
      provider.address = "https://ai-gateway.vercel.sh/v1";
      break;
    case ProviderPreset::kOllama:
      provider.name = "Ollama";
      provider.address = "http://localhost:11434/v1";
      break;
    case ProviderPreset::kJev:
      provider.name = "Jev";
      provider.address = "https://api.typesafe.ai/v1";
      provider.format = ProviderFormat::kSystemOne;
      break;
  }
  return provider;
}

std::vector<Provider> ReadProviders(const PrefService& prefs) {
  std::vector<Provider> providers;
  for (const base::Value& entry : prefs.GetList(kProvidersPref)) {
    if (std::optional<Provider> provider = ProviderFrom(entry)) {
      providers.push_back(std::move(*provider));
    }
  }
  return providers;
}

std::optional<Provider> FindProvider(const PrefService& prefs,
                                     std::string_view id) {
  for (Provider& provider : ReadProviders(prefs)) {
    if (provider.id == id) {
      return std::move(provider);
    }
  }
  return std::nullopt;
}

std::string AddProvider(PrefService& prefs, Provider provider) {
  std::vector<std::string> taken;
  for (const Provider& saved : ReadProviders(prefs)) {
    taken.push_back(saved.id);
  }
  const std::string slug = Slug(provider.preset);
  provider.id = slug;
  for (int n = 2; std::ranges::contains(taken, provider.id); ++n) {
    provider.id = base::StrCat({slug, "-", base::NumberToString(n)});
  }
  ScopedListPrefUpdate(&prefs, kProvidersPref)->Append(DictFrom(provider));
  return provider.id;
}

bool UpdateProvider(PrefService& prefs, const Provider& provider) {
  ScopedListPrefUpdate update(&prefs, kProvidersPref);
  for (base::Value& entry : *update) {
    const std::optional<Provider> saved = ProviderFrom(entry);
    if (saved && saved->id == provider.id) {
      entry = base::Value(DictFrom(provider));
      return true;
    }
  }
  return false;
}

void RemoveProvider(PrefService& prefs, std::string_view id) {
  ScopedListPrefUpdate(&prefs, kProvidersPref)
      ->EraseIf([id](const base::Value& entry) {
        const std::optional<Provider> saved = ProviderFrom(entry);
        return saved && saved->id == id;
      });
}

std::string KeyAccountFor(std::string_view provider_id) {
  return base::StrCat({"provider/", provider_id});
}

std::optional<ModelChoice> ReadChoice(const PrefService& prefs,
                                      ModelSlot slot) {
  const base::DictValue& dict = prefs.GetDict(ChoicePref(slot));
  const std::string* provider = dict.FindString(kProvider);
  const std::string* model = dict.FindString(kModel);
  if (!provider || provider->empty() || !model || model->empty()) {
    return std::nullopt;
  }
  const std::string* effort = dict.FindString(kEffort);
  return ModelChoice{*provider, *model, effort ? *effort : std::string()};
}

void SetChoice(PrefService& prefs,
               ModelSlot slot,
               std::string_view provider_id,
               std::string_view model) {
  prefs.SetDict(
      ChoicePref(slot),
      base::DictValue().Set(kProvider, provider_id).Set(kModel, model));
}

void SetEffort(PrefService& prefs, ModelSlot slot, std::string_view effort) {
  if (ReadChoice(prefs, slot)) {
    ScopedDictPrefUpdate(&prefs, ChoicePref(slot))->Set(kEffort, effort);
  }
}

void ClearChoice(PrefService& prefs, ModelSlot slot) {
  prefs.ClearPref(ChoicePref(slot));
}

std::optional<ProviderFormat> LanguageFormatOf(const Provider& provider) {
  switch (provider.format) {
    case ProviderFormat::kChatCompletions:
    case ProviderFormat::kAnthropicMessages:
    case ProviderFormat::kOpenAiResponses:
      return provider.format;
    case ProviderFormat::kOpenAiDecisions:
    case ProviderFormat::kSystemOne:
      return std::nullopt;
  }
}

std::optional<ProviderFormat> DecisionFormatOf(const Provider& provider) {
  if (provider.preset == ProviderPreset::kOpenAi) {
    return ProviderFormat::kOpenAiDecisions;
  }
  switch (provider.format) {
    case ProviderFormat::kOpenAiDecisions:
    case ProviderFormat::kSystemOne:
      return provider.format;
    case ProviderFormat::kChatCompletions:
    case ProviderFormat::kAnthropicMessages:
    case ProviderFormat::kOpenAiResponses:
      return std::nullopt;
  }
}

}  // namespace uw
