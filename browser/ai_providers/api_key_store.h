// Copyright 2026 The uw Authors. All rights reserved.

#ifndef UW_BROWSER_AI_PROVIDERS_API_KEY_STORE_H_
#define UW_BROWSER_AI_PROVIDERS_API_KEY_STORE_H_

#include <optional>
#include <string>

#include "uw/browser/branding/identity_buildflags.h"

namespace uw {

// Stores provider API keys in macOS Keychain under non-secret account ids.
// The account id itself (which provider slot the key fills) lives in profile
// prefs; only the secret lives here. Never log keys. A run started with
// --use-mock-keychain keeps its keys in memory and leaves the Keychain alone.
class ApiKeyStore {
 public:
  ApiKeyStore();
  ~ApiKeyStore();
  ApiKeyStore(const ApiKeyStore&) = delete;
  ApiKeyStore& operator=(const ApiKeyStore&) = delete;

  // Creates or replaces the key for `account`. Empty keys are rejected.
  bool SetKey(const std::string& account, const std::string& key);
  std::optional<std::string> GetKey(const std::string& account);
  bool DeleteKey(const std::string& account);

 private:
  const std::string service_name_;
};

#if BUILDFLAG(UW_DEV)
// The first time uw dev starts with no keys of its own, it copies the
// installed uw's model keys, and macOS asks for the login password once for
// each. Connector sign-ins stay with the installed uw. A run with
// --use-mock-keychain copies nothing. Blocks until the user answers.
void CopyModelKeysFromInstalledUw();
#endif  // BUILDFLAG(UW_DEV)

}  // namespace uw

#endif  // UW_BROWSER_AI_PROVIDERS_API_KEY_STORE_H_
