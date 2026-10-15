// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/browser/ai_providers/scoped_memory_keychain.h"

#include <Security/Security.h>

#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "base/apple/foundation_util.h"
#include "base/containers/span.h"
#include "base/strings/sys_string_conversions.h"
#include "base/types/expected.h"
#include "crypto/apple/keychain_v2.h"

namespace uw {

class ScopedMemoryKeychain::Keychain : public crypto::apple::KeychainV2 {
 public:
  Keychain() { SetInstanceOverride(this); }
  ~Keychain() override { ClearInstanceOverride(); }

  base::expected<std::vector<uint8_t>, OSStatus> FindGenericPassword(
      std::string_view service_name,
      std::string_view account_name) override {
    const auto password = passwords_.find(std::string(account_name));
    if (password == passwords_.end()) {
      return base::unexpected(errSecItemNotFound);
    }
    return password->second;
  }

  OSStatus AddGenericPassword(std::string_view service_name,
                              std::string_view account_name,
                              base::span<const uint8_t> password) override {
    passwords_[std::string(account_name)] =
        std::vector<uint8_t>(password.begin(), password.end());
    return noErr;
  }

  OSStatus ItemDelete(CFDictionaryRef query) override {
    const CFStringRef account =
        base::apple::GetValueFromDictionary<CFStringRef>(query,
                                                         kSecAttrAccount);
    if (account && passwords_.erase(base::SysCFStringRefToUTF8(account))) {
      return noErr;
    }
    return errSecItemNotFound;
  }

 private:
  std::map<std::string, std::vector<uint8_t>> passwords_;
};

ScopedMemoryKeychain::ScopedMemoryKeychain()
    : keychain_(std::make_unique<Keychain>()) {}

ScopedMemoryKeychain::~ScopedMemoryKeychain() = default;

}  // namespace uw
