// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/browser/ai_providers/models.h"

#include <algorithm>
#include <string>
#include <vector>

#include "base/json/json_reader.h"
#include "base/strings/string_util.h"
#include "base/values.h"
#include "components/prefs/pref_registry_simple.h"
#include "components/prefs/pref_service.h"
#include "net/base/url_util.h"
#include "url/gurl.h"
#include "uw/browser/ai_providers/api_key_store.h"
#include "uw/components/ai_providers/formats.h"

namespace uw {
namespace {

// What kDecisionProviderPref held before uw://providers.
constexpr int kOldJev = 1;
constexpr int kOldOpenAiCompatible = 3;
constexpr char kOldJevModel[] = "jev-latest";
constexpr char kOldOpenAiDecisionsModel[] = "gpt-6-luna";

GURL EndpointUrl(std::string_view endpoint, std::string_view suffix) {
  std::string base(endpoint);
  while (base.ends_with('/')) {
    base.pop_back();
  }
  return GURL(base + std::string(suffix));
}

// One connection saved before uw://providers.
struct OldConnection {
  ModelSlot slot;
  Provider provider;
  std::string model;
  std::string key;
  // Its Keychain account, removed once every key has moved.
  std::string account;
};

bool ServesSlot(const Provider& provider, ModelSlot slot) {
  return slot == ModelSlot::kDecision ? DecisionFormatOf(provider).has_value()
                                      : LanguageFormatOf(provider).has_value();
}

// The preset at `address` when it serves `slot`, or a Custom provider named
// after the address's host that speaks `format`.
Provider ProviderAt(const std::string& address,
                    ModelSlot slot,
                    ProviderFormat format) {
  const GURL url(address);
  for (ProviderPreset preset :
       {ProviderPreset::kOpenAi, ProviderPreset::kAnthropic,
        ProviderPreset::kOpenRouter, ProviderPreset::kVercelAiGateway,
        ProviderPreset::kOllama, ProviderPreset::kJev}) {
    Provider provider = PresetProvider(preset);
    const GURL known(provider.address);
    if (known.host() == url.host() &&
        known.EffectiveIntPort() == url.EffectiveIntPort() &&
        ServesSlot(provider, slot)) {
      provider.address = address;
      return provider;
    }
  }
  Provider provider = PresetProvider(ProviderPreset::kCustom);
  provider.name = url.host();
  provider.address = address;
  provider.format = format;
  return provider;
}

std::vector<OldConnection> OldConnections(const PrefService& prefs) {
  ApiKeyStore keys;
  std::vector<OldConnection> old;
  const auto add = [&](ModelSlot slot, const std::string& address,
                       ProviderFormat format, const std::string& model,
                       const char* account_pref) {
    if (!IsValidProviderEndpoint(address) || model.empty()) {
      return;
    }
    const std::string account = prefs.GetString(account_pref);
    old.push_back(
        {slot, ProviderAt(address, slot, format), model,
         account.empty() ? std::string() : keys.GetKey(account).value_or(""),
         account});
  };
  add(ModelSlot::kLanguage, prefs.GetString(kOrganizationEndpointPref),
      ProviderFormat::kChatCompletions, prefs.GetString(kOrganizationModelPref),
      kOrganizationKeyAccountPref);
  const int decision = prefs.GetInteger(kDecisionProviderPref);
  if (decision >= kOldJev && decision <= kOldOpenAiCompatible) {
    std::string address = prefs.GetString(kDecisionEndpointPref);
    std::string model = prefs.GetString(kDecisionModelPref);
    if (decision != kOldOpenAiCompatible) {
      const bool jev = decision == kOldJev;
      if (address.empty()) {
        address =
            PresetProvider(jev ? ProviderPreset::kJev : ProviderPreset::kOpenAi)
                .address;
      }
      if (model.empty()) {
        model = jev ? kOldJevModel : kOldOpenAiDecisionsModel;
      }
    }
    add(ModelSlot::kDecision, address,
        decision == kOldJev ? ProviderFormat::kSystemOne
                            : ProviderFormat::kOpenAiDecisions,
        model, kDecisionKeyAccountPref);
  }
  add(ModelSlot::kAgent, prefs.GetString(kAgentEndpointPref),
      ProviderFormat::kChatCompletions, prefs.GetString(kAgentModelPref),
      kAgentKeyAccountPref);
  return old;
}

// Connections with the same address and key share one provider, when that
// provider can serve both.
bool SameProvider(const OldConnection& a, const OldConnection& b) {
  std::string first = a.provider.address;
  std::string second = b.provider.address;
  while (first.ends_with('/')) {
    first.pop_back();
  }
  while (second.ends_with('/')) {
    second.pop_back();
  }
  return first == second && a.key == b.key &&
         a.provider.preset == b.provider.preset &&
         a.provider.format == b.provider.format;
}

}  // namespace

AiConnectionConfig ConfigForChoice(const PrefService& prefs,
                                   const ModelChoice& choice) {
  AiConnectionConfig config;
  const std::optional<Provider> provider =
      FindProvider(prefs, choice.provider_id);
  const std::optional<ProviderFormat> format =
      provider ? LanguageFormatOf(*provider) : std::nullopt;
  if (!format) {
    return config;
  }
  config.endpoint = provider->address;
  config.preset = provider->preset;
  config.format = *format;
  config.model = choice.model;
  config.effort = choice.effort;
  config.key_account = provider->key_account;
  return config;
}

void RegisterModelPrefs(PrefRegistrySimple* registry) {
  for (const char* pref :
       {kOrganizationEndpointPref, kOrganizationModelPref,
        kOrganizationKeyAccountPref, kDecisionEndpointPref, kDecisionModelPref,
        kDecisionKeyAccountPref, kAgentEndpointPref, kAgentModelPref,
        kAgentKeyAccountPref}) {
    registry->RegisterStringPref(pref, std::string());
  }
  for (const char* pref :
       {kOrganizationModePref, kDecisionProviderPref, kAgentModePref}) {
    registry->RegisterIntegerPref(pref, 0);
  }
  registry->RegisterIntegerPref(kUpgradeNoticePref,
                                static_cast<int>(UpgradeNotice::kNone));
  RegisterProviderPrefs(registry);
}

void MoveConnectionsToProviders(PrefService* prefs, bool tested) {
  std::vector<OldConnection> old = OldConnections(*prefs);
  if (old.empty()) {
    return;
  }
  ApiKeyStore keys;
  std::vector<std::string> ids;
  std::vector<std::string> added;
  bool merged = false;
  for (size_t i = 0; i < old.size(); ++i) {
    const auto shared = std::find_if(old.begin(), old.begin() + i,
                                     [&](const OldConnection& earlier) {
                                       return SameProvider(earlier, old[i]);
                                     });
    if (shared != old.begin() + i) {
      ids.push_back(ids[shared - old.begin()]);
      merged = true;
      continue;
    }
    Provider& provider = old[i].provider;
    provider.id = AddProvider(*prefs, provider);
    added.push_back(provider.id);
    ids.push_back(provider.id);
    if (old[i].key.empty()) {
      continue;
    }
    provider.key_account = KeyAccountFor(provider.id);
    if (!keys.SetKey(provider.key_account, old[i].key) ||
        keys.GetKey(provider.key_account) != old[i].key) {
      for (const std::string& id : added) {
        keys.DeleteKey(KeyAccountFor(id));
        RemoveProvider(*prefs, id);
      }
      prefs->SetInteger(kUpgradeNoticePref,
                        static_cast<int>(UpgradeNotice::kFailed));
      return;
    }
    UpdateProvider(*prefs, provider);
  }
  for (size_t i = 0; i < old.size(); ++i) {
    SetChoice(*prefs, old[i].slot, ids[i], old[i].model);
    if (tested && old[i].slot != ModelSlot::kAgent) {
      MarkTested(*prefs, old[i].slot, *ReadChoice(*prefs, old[i].slot));
    }
  }
  for (const char* pref :
       {kOrganizationModePref, kOrganizationEndpointPref,
        kOrganizationModelPref, kOrganizationKeyAccountPref,
        kDecisionProviderPref, kDecisionEndpointPref, kDecisionModelPref,
        kDecisionKeyAccountPref, kAgentModePref, kAgentEndpointPref,
        kAgentModelPref, kAgentKeyAccountPref}) {
    prefs->ClearPref(pref);
  }
  for (const OldConnection& connection : old) {
    if (!connection.account.empty()) {
      keys.DeleteKey(connection.account);
    }
  }
  prefs->SetInteger(kUpgradeNoticePref,
                    static_cast<int>(merged ? UpgradeNotice::kMerged
                                            : UpgradeNotice::kMoved));
}

AiConnectionConfig ReadAiConnectionConfig(const PrefService* prefs) {
  const std::optional<ModelChoice> choice =
      prefs ? ReadChoice(*prefs, ModelSlot::kLanguage) : std::nullopt;
  return choice ? ConfigForChoice(*prefs, *choice) : AiConnectionConfig();
}

std::optional<ModelChoice> AgentChoice(const PrefService& prefs) {
  std::optional<ModelChoice> agent = ReadChoice(prefs, ModelSlot::kAgent);
  return agent ? agent : ReadChoice(prefs, ModelSlot::kLanguage);
}

DecisionModelConfig ReadDecisionModelConfig(const PrefService* prefs) {
  DecisionModelConfig config;
  const std::optional<ModelChoice> choice =
      prefs ? ReadChoice(*prefs, ModelSlot::kDecision) : std::nullopt;
  const std::optional<Provider> provider =
      choice ? FindProvider(*prefs, choice->provider_id) : std::nullopt;
  const std::optional<ProviderFormat> format =
      provider ? DecisionFormatOf(*provider) : std::nullopt;
  if (!format) {
    return config;
  }
  config.format = format == ProviderFormat::kSystemOne
                      ? DecisionFormat::kSystemOne
                      : DecisionFormat::kDecisions;
  config.endpoint = provider->address;
  config.model = choice->model;
  config.key_account = provider->key_account;
  return config;
}

bool IsValidProviderEndpoint(std::string_view endpoint) {
  const GURL url(endpoint);
  if (!url.is_valid() || url.has_username() || url.has_password() ||
      url.has_ref()) {
    return false;
  }
  if (url.SchemeIs("https")) {
    return true;
  }
  return url.SchemeIs("http") && net::IsLocalhost(url);
}

GURL LanguageModelUrl(std::string_view endpoint, ProviderFormat format) {
  return EndpointUrl(endpoint, LanguageModelPath(format));
}

GURL ModelsUrl(std::string_view endpoint) {
  return EndpointUrl(endpoint, "/models");
}

GURL DecisionUrl(const DecisionModelConfig& config) {
  return EndpointUrl(config.endpoint,
                     config.format == DecisionFormat::kSystemOne
                         ? "/systemone"
                         : "/decisions");
}

bool IsComplete(const AiConnectionConfig& config) {
  return IsValidProviderEndpoint(config.endpoint) && !config.model.empty();
}

bool IsComplete(const DecisionModelConfig& config) {
  return IsValidProviderEndpoint(config.endpoint) && !config.model.empty();
}

std::string ProviderMessage(const std::string& body) {
  constexpr size_t kMaxMessageBytes = 200;
  const std::optional<base::DictValue> parsed =
      base::JSONReader::ReadDict(body, base::JSON_PARSE_RFC);
  if (!parsed) {
    return std::string();
  }
  const std::string* message = nullptr;
  for (const char* field : {"error", "detail", "message"}) {
    const base::DictValue* nested = parsed->FindDict(field);
    if (!message) {
      message =
          nested ? nested->FindString("message") : parsed->FindString(field);
    }
  }
  return message ? std::string(
                       base::TruncateUTF8ToByteSize(*message, kMaxMessageBytes))
                 : std::string();
}

std::optional<ModelSlot> UntestedModel(const PrefService* prefs) {
  for (ModelSlot slot : {ModelSlot::kLanguage, ModelSlot::kDecision}) {
    const std::optional<ModelChoice> choice =
        prefs ? ReadChoice(*prefs, slot) : std::nullopt;
    if (choice && !choice->tested) {
      return slot;
    }
  }
  return std::nullopt;
}

}  // namespace uw
