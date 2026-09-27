// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/browser/tab_organization/ai_connection.h"

#include "base/strings/string_util.h"
#include "components/pref_registry/pref_registry_syncable.h"
#include "components/prefs/pref_service.h"
#include "net/base/url_util.h"
#include "url/gurl.h"
#include "uw/components/tab_organization/ai_protocol.h"

namespace uw {

void RegisterAiConnectionProfilePrefs(
    user_prefs::PrefRegistrySyncable* registry) {
  registry->RegisterStringPref(kJevEndpointPref, kJevDefaultEndpoint);
  registry->RegisterStringPref(kJevKeyAccountPref, std::string());
  registry->RegisterStringPref(kNamingEndpointPref, std::string());
  registry->RegisterStringPref(kNamingModelPref, std::string());
  registry->RegisterStringPref(kNamingKeyAccountPref, std::string());
  registry->RegisterBooleanPref(kOrganizationEnabledPref, false);
}

AiConnectionConfig ReadAiConnectionConfig(const PrefService* prefs) {
  AiConnectionConfig config;
  if (!prefs) {
    return config;
  }
  config.jev_endpoint = prefs->GetString(kJevEndpointPref);
  config.jev_key_account = prefs->GetString(kJevKeyAccountPref);
  config.naming_endpoint = prefs->GetString(kNamingEndpointPref);
  config.naming_model = prefs->GetString(kNamingModelPref);
  config.naming_key_account = prefs->GetString(kNamingKeyAccountPref);
  config.organization_enabled = prefs->GetBoolean(kOrganizationEnabledPref);
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

bool CanOrganize(const AiConnectionConfig& config) {
  return config.organization_enabled &&
         IsValidProviderEndpoint(config.jev_endpoint) &&
         !config.jev_key_account.empty() &&
         IsValidProviderEndpoint(config.naming_endpoint) &&
         !config.naming_model.empty() && !config.naming_key_account.empty();
}

}  // namespace uw
