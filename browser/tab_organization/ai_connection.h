// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#ifndef UW_BROWSER_TAB_ORGANIZATION_AI_CONNECTION_H_
#define UW_BROWSER_TAB_ORGANIZATION_AI_CONNECTION_H_

#include <string>

class PrefService;
namespace user_prefs {
class PrefRegistrySyncable;
}

namespace uw {

// Profile preferences for the two remote providers behind automatic
// organization. Secrets live in macOS Keychain under non-secret account ids;
// only endpoints, model ids, and enablement live here.
inline constexpr char kJevEndpointPref[] = "uw.ai.jev_endpoint";
inline constexpr char kJevKeyAccountPref[] = "uw.ai.jev_key_account";
inline constexpr char kNamingEndpointPref[] = "uw.ai.naming_endpoint";
inline constexpr char kNamingModelPref[] = "uw.ai.naming_model";
inline constexpr char kNamingKeyAccountPref[] = "uw.ai.naming_key_account";
inline constexpr char kOrganizationEnabledPref[] =
    "uw.ai.organization_enabled";

struct AiConnectionConfig {
  std::string jev_endpoint;
  std::string jev_key_account;
  std::string naming_endpoint;
  std::string naming_model;
  std::string naming_key_account;
  bool organization_enabled = false;
};

void RegisterAiConnectionProfilePrefs(
    user_prefs::PrefRegistrySyncable* registry);
AiConnectionConfig ReadAiConnectionConfig(const PrefService* prefs);

// Accepts https URLs, plus http loopback for local testing. Rejects
// credentials, fragments, and invalid URLs.
bool IsValidProviderEndpoint(std::string_view endpoint);

// True when automatic organization may dispatch: explicitly enabled with both
// provider endpoints valid, both Keychain accounts present, and a naming
// model selected.
bool CanOrganize(const AiConnectionConfig& config);

}  // namespace uw

#endif  // UW_BROWSER_TAB_ORGANIZATION_AI_CONNECTION_H_
