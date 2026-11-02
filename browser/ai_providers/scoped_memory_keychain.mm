// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/browser/ai_providers/scoped_memory_keychain.h"

#include <Security/Security.h>

#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "base/apple/bridging.h"
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
    const auto password =
        passwords_.find({std::string(service_name), std::string(account_name)});
    if (password == passwords_.end()) {
      return base::unexpected(errSecItemNotFound);
    }
    return password->second;
  }

  OSStatus AddGenericPassword(std::string_view service_name,
                              std::string_view account_name,
                              base::span<const uint8_t> password) override {
    passwords_[{std::string(service_name), std::string(account_name)}] =
        std::vector<uint8_t>(password.begin(), password.end());
    return noErr;
  }

  // Lists the accounts of one service, as a query for all of a service's
  // items and their attributes does.
  OSStatus ItemCopyMatching(CFDictionaryRef query, CFTypeRef* result) override {
    const std::string service = Attribute(query, kSecAttrService);
    NSMutableArray* items = [NSMutableArray array];
    for (const auto& [key, password] : passwords_) {
      if (key.first == service) {
        [items addObject:@{
          base::apple::CFToNSPtrCast(kSecAttrAccount) :
              base::SysUTF8ToNSString(key.second)
        }];
      }
    }
    if (items.count == 0) {
      return errSecItemNotFound;
    }
    if (result) {
      NSArray* found = [items copy];
      *result = base::apple::NSToCFOwnershipCast(found);
    }
    return noErr;
  }

  OSStatus ItemDelete(CFDictionaryRef query) override {
    if (passwords_.erase({Attribute(query, kSecAttrService),
                          Attribute(query, kSecAttrAccount)})) {
      return noErr;
    }
    return errSecItemNotFound;
  }

 private:
  static std::string Attribute(CFDictionaryRef query, CFStringRef key) {
    const CFStringRef value =
        base::apple::GetValueFromDictionary<CFStringRef>(query, key);
    return value ? base::SysCFStringRefToUTF8(value) : std::string();
  }

  // By service, then account.
  std::map<std::pair<std::string, std::string>, std::vector<uint8_t>>
      passwords_;
};

ScopedMemoryKeychain::ScopedMemoryKeychain()
    : keychain_(std::make_unique<Keychain>()) {}

ScopedMemoryKeychain::~ScopedMemoryKeychain() = default;

}  // namespace uw
