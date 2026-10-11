// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/browser/tab_organization/ai_connection.h"

#include <string>

#include "components/pref_registry/pref_registry_syncable.h"
#include "components/prefs/pref_service.h"
#include "net/base/url_util.h"
#include "url/gurl.h"

namespace uw {
namespace {

// "Pause until next launch" must reset when uw next starts, so it lives in
// process state rather than in a profile preference.
bool g_paused_until_launch = false;

GURL EndpointUrl(std::string_view endpoint, std::string_view suffix) {
  std::string base(endpoint);
  while (base.ends_with('/')) {
    base.pop_back();
  }
  return GURL(base + std::string(suffix));
}

}  // namespace

void RegisterAiConnectionProfilePrefs(
    user_prefs::PrefRegistrySyncable* registry) {
  registry->RegisterIntegerPref(kOrganizationModePref,
                                static_cast<int>(ConnectionMode::kOllama));
  registry->RegisterStringPref(kOrganizationEndpointPref, std::string());
  registry->RegisterStringPref(kOrganizationModelPref, std::string());
  registry->RegisterStringPref(kOrganizationKeyAccountPref, std::string());
  registry->RegisterBooleanPref(kOrganizationEnabledPref, false);
  registry->RegisterBooleanPref(kOrganizationShowActivityPref, false);
  registry->RegisterIntegerPref(kDecisionProviderPref,
                                static_cast<int>(DecisionProvider::kNone));
  registry->RegisterStringPref(kDecisionEndpointPref, std::string());
  registry->RegisterStringPref(kDecisionModelPref, std::string());
  registry->RegisterStringPref(kDecisionKeyAccountPref, std::string());
  registry->RegisterIntegerPref(kOrganizationPauseModePref,
                                static_cast<int>(PauseMode::kOff));
  registry->RegisterDoublePref(kOrganizationPauseUntilPref, 0);
}

AiConnectionConfig ReadAiConnectionConfig(const PrefService* prefs) {
  AiConnectionConfig config;
  if (!prefs) {
    return config;
  }
  config.mode = prefs->GetInteger(kOrganizationModePref) ==
                        static_cast<int>(ConnectionMode::kOpenAiCompatible)
                    ? ConnectionMode::kOpenAiCompatible
                    : ConnectionMode::kOllama;
  config.endpoint = prefs->GetString(kOrganizationEndpointPref);
  config.model = prefs->GetString(kOrganizationModelPref);
  config.key_account = prefs->GetString(kOrganizationKeyAccountPref);
  return config;
}

DecisionModelConfig ReadDecisionModelConfig(const PrefService* prefs) {
  DecisionModelConfig config;
  if (!prefs) {
    return config;
  }
  const int provider = prefs->GetInteger(kDecisionProviderPref);
  if (provider < static_cast<int>(DecisionProvider::kJev) ||
      provider > static_cast<int>(DecisionProvider::kOpenAiCompatible)) {
    return config;
  }
  config.provider = static_cast<DecisionProvider>(provider);
  config.endpoint = prefs->GetString(kDecisionEndpointPref);
  config.model = prefs->GetString(kDecisionModelPref);
  config.key_account = prefs->GetString(kDecisionKeyAccountPref);
  const bool jev = config.provider == DecisionProvider::kJev;
  if (config.endpoint.empty() &&
      config.provider != DecisionProvider::kOpenAiCompatible) {
    config.endpoint = jev ? kJevDefaultEndpoint : kOpenAiDefaultEndpoint;
  }
  if (config.model.empty() &&
      config.provider != DecisionProvider::kOpenAiCompatible) {
    config.model = jev ? kJevDefaultModel : kOpenAiDecisionsDefaultModel;
  }
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

DecisionFormat FormatFor(DecisionProvider provider) {
  return provider == DecisionProvider::kJev ? DecisionFormat::kSystemOne
                                            : DecisionFormat::kDecisions;
}

GURL DecisionUrl(const DecisionModelConfig& config) {
  return EndpointUrl(config.endpoint, config.provider == DecisionProvider::kJev
                                          ? "/systemone"
                                          : "/decisions");
}

bool IsComplete(const AiConnectionConfig& config) {
  return IsValidProviderEndpoint(config.endpoint) && !config.model.empty() &&
         (config.mode == ConnectionMode::kOllama ||
          !config.key_account.empty());
}

bool IsComplete(const DecisionModelConfig& config) {
  return config.provider != DecisionProvider::kNone &&
         IsValidProviderEndpoint(config.endpoint) && !config.model.empty() &&
         !config.key_account.empty();
}

bool OrganizationReady(const PrefService* prefs) {
  return prefs && prefs->GetBoolean(kOrganizationEnabledPref) &&
         (IsComplete(ReadDecisionModelConfig(prefs)) ||
          IsComplete(ReadAiConnectionConfig(prefs)));
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
