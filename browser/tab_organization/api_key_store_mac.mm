// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/browser/tab_organization/api_key_store.h"

#include <vector>

#include <Security/Security.h>

#include "base/apple/scoped_cftyperef.h"
#include "base/containers/span.h"
#include "crypto/apple/keychain_v2.h"

namespace uw {

ApiKeyStore::ApiKeyStore() : service_name_("uw-ai") {}
ApiKeyStore::~ApiKeyStore() = default;

bool ApiKeyStore::SetKey(const std::string& account, const std::string& key) {
  if (account.empty() || key.empty()) {
    return false;
  }
  const OSStatus status =
      crypto::apple::KeychainV2::GetInstance().AddGenericPassword(
          service_name_, account, base::as_bytes(base::span(key)));
  return status == errSecSuccess;
}

std::optional<std::string> ApiKeyStore::GetKey(const std::string& account) {
  if (account.empty()) {
    return std::nullopt;
  }
  auto result =
      crypto::apple::KeychainV2::GetInstance().FindGenericPassword(
          service_name_, account);
  if (!result.has_value()) {
    return std::nullopt;
  }
  return std::string(result->begin(), result->end());
}

bool ApiKeyStore::DeleteKey(const std::string& account) {
  if (account.empty()) {
    return false;
  }
  // KeychainV2 has no delete-by-account helper; delete via a matching query.
  base::apple::ScopedCFTypeRef<CFMutableDictionaryRef> query(
      CFDictionaryCreateMutable(kCFAllocatorDefault, 0,
                                &kCFTypeDictionaryKeyCallBacks,
                                &kCFTypeDictionaryValueCallBacks));
  if (!query) {
    return false;
  }
  base::apple::ScopedCFTypeRef<CFStringRef> service(
      CFStringCreateWithCString(kCFAllocatorDefault, service_name_.c_str(),
                                kCFStringEncodingUTF8));
  base::apple::ScopedCFTypeRef<CFStringRef> account_ref(
      CFStringCreateWithCString(kCFAllocatorDefault, account.c_str(),
                                kCFStringEncodingUTF8));
  if (!service || !account_ref) {
    return false;
  }
  CFDictionarySetValue(query.get(), kSecClass, kSecClassGenericPassword);
  CFDictionarySetValue(query.get(), kSecAttrService, service.get());
  CFDictionarySetValue(query.get(), kSecAttrAccount, account_ref.get());
  return crypto::apple::KeychainV2::GetInstance().ItemDelete(query.get()) ==
         errSecSuccess;
}

}  // namespace uw
