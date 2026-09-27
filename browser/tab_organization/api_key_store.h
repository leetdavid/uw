// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#ifndef UW_BROWSER_TAB_ORGANIZATION_API_KEY_STORE_H_
#define UW_BROWSER_TAB_ORGANIZATION_API_KEY_STORE_H_

#include <optional>
#include <string>

namespace uw {

// Stores provider API keys in macOS Keychain under non-secret account ids.
// The account id itself (which provider slot the key fills) lives in profile
// prefs; only the secret lives here. Never log keys.
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

}  // namespace uw

#endif  // UW_BROWSER_TAB_ORGANIZATION_API_KEY_STORE_H_
