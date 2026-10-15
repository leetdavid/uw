// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/browser/tab_organization/ai_connection.h"

#include <algorithm>
#include <string>
#include <vector>

#include "components/pref_registry/pref_registry_syncable.h"
#include "components/prefs/pref_service.h"
#include "net/base/url_util.h"
#include "url/gurl.h"
#include "uw/browser/tab_organization/api_key_store.h"

namespace uw {
namespace {

// "Pause until next launch" must reset when uw next starts, so it lives in
// process state rather than in a profile preference.
bool g_paused_until_launch = false;

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

AiConnectionConfig ConfigFor(const PrefService* prefs, ModelSlot slot) {
  AiConnectionConfig config;
  const std::optional<ModelChoice> choice =
      prefs ? ReadChoice(*prefs, slot) : std::nullopt;
  const std::optional<Provider> provider =
      choice ? FindProvider(*prefs, choice->provider_id) : std::nullopt;
  if (!provider || !LanguageFormatOf(*provider)) {
    return config;
  }
  config.endpoint = provider->address;
  config.model = choice->model;
  config.key_account = provider->key_account;
  return config;
}

}  // namespace

void RegisterAiConnectionProfilePrefs(
    user_prefs::PrefRegistrySyncable* registry) {
  registry->RegisterBooleanPref(kOrganizationEnabledPref, false);
  registry->RegisterBooleanPref(kOrganizationShowActivityPref, false);
  registry->RegisterDoublePref(kOrganizationMoveThresholdPref,
                               kDefaultThresholds.confidence);
  registry->RegisterDoublePref(kOrganizationNewTaskThresholdPref,
                               kDefaultThresholds.new_task);
  registry->RegisterDoublePref(kOrganizationRenameThresholdPref,
                               kDefaultThresholds.new_name);
  registry->RegisterListPref(kAgentDisabledSkillsPref);
  registry->RegisterBooleanPref(kCommandBarEnabledPref, true);
  registry->RegisterIntegerPref(kOrganizationPauseModePref,
                                static_cast<int>(PauseMode::kOff));
  registry->RegisterDoublePref(kOrganizationPauseUntilPref, 0);
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

void MoveConnectionsToProviders(PrefService* prefs) {
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
  // Saving a connection tested it, and automatic organization was on only
  // while every saved one passed.
  const bool tested = prefs->GetBoolean(kOrganizationEnabledPref);
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
  return ConfigFor(prefs, ModelSlot::kLanguage);
}

AiConnectionConfig ReadAgentModelConfig(const PrefService* prefs) {
  return prefs && ReadChoice(*prefs, ModelSlot::kAgent)
             ? ConfigFor(prefs, ModelSlot::kAgent)
             : ConfigFor(prefs, ModelSlot::kLanguage);
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

GURL ChatCompletionsUrl(std::string_view endpoint) {
  return EndpointUrl(endpoint, "/chat/completions");
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

bool OrganizationReady(const PrefService* prefs) {
  return prefs && prefs->GetBoolean(kOrganizationEnabledPref) &&
         !UntestedModel(prefs) &&
         (IsComplete(ReadDecisionModelConfig(prefs)) ||
          IsComplete(ReadAiConnectionConfig(prefs)));
}

Thresholds ReadThresholds(const PrefService* prefs) {
  return {AsThreshold(prefs->GetDouble(kOrganizationMoveThresholdPref)),
          AsThreshold(prefs->GetDouble(kOrganizationNewTaskThresholdPref)),
          AsThreshold(prefs->GetDouble(kOrganizationRenameThresholdPref))};
}

void SaveThresholds(PrefService* prefs, const Thresholds& thresholds) {
  prefs->SetDouble(kOrganizationMoveThresholdPref,
                   AsThreshold(thresholds.confidence));
  prefs->SetDouble(kOrganizationNewTaskThresholdPref,
                   AsThreshold(thresholds.new_task));
  prefs->SetDouble(kOrganizationRenameThresholdPref,
                   AsThreshold(thresholds.new_name));
}

PauseMode ReadPauseState(const PrefService* prefs) {
  if (g_paused_until_launch) {
    return PauseMode::kUntilLaunch;
  }
  if (!prefs) {
    return PauseMode::kOff;
  }
  switch (prefs->GetInteger(kOrganizationPauseModePref)) {
    case static_cast<int>(PauseMode::kTimed): {
      const base::Time until = base::Time::FromDeltaSinceWindowsEpoch(
          base::Microseconds(static_cast<int64_t>(
              prefs->GetDouble(kOrganizationPauseUntilPref))));
      return base::Time::Now() < until ? PauseMode::kTimed : PauseMode::kOff;
    }
    case static_cast<int>(PauseMode::kIndefinite):
      return PauseMode::kIndefinite;
    default:
      return PauseMode::kOff;
  }
}

void SetPauseState(PrefService* prefs,
                   PauseMode mode,
                   base::TimeDelta duration) {
  g_paused_until_launch = mode == PauseMode::kUntilLaunch;
  if (!prefs) {
    return;
  }
  prefs->SetInteger(kOrganizationPauseModePref, static_cast<int>(mode));
  // The timed-pause deadline is stored in microseconds since the Windows
  // epoch.
  prefs->SetDouble(kOrganizationPauseUntilPref,
                   mode == PauseMode::kTimed ? (base::Time::Now() + duration)
                                                   .ToDeltaSinceWindowsEpoch()
                                                   .InMicroseconds()
                                             : 0);
}

}  // namespace uw
