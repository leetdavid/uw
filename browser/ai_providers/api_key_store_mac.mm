// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/browser/ai_providers/api_key_store.h"

#include <Security/Security.h>

#include <map>

#include "base/apple/scoped_cftyperef.h"
#include "base/command_line.h"
#include "base/containers/span.h"
#include "base/no_destructor.h"
#include "components/os_crypt/common/os_crypt_switches.h"
#include "crypto/apple/keychain_v2.h"

namespace uw {
namespace {

// Where the keys of a run with --use-mock-keychain live: in this process.
// Tests and scripted runs of the app pass that switch, and the Keychain items
// are shared by every profile on the Mac, so such a run must neither read nor
// replace the keys the user saved. Null in every other run.
std::map<std::string, std::string>* MemoryKeys() {
  static base::NoDestructor<std::map<std::string, std::string>> keys;
  return base::CommandLine::ForCurrentProcess()->HasSwitch(
             os_crypt::switches::kUseMockKeychain)
             ? keys.get()
             : nullptr;
}

}  // namespace

ApiKeyStore::ApiKeyStore() : service_name_("uw-ai") {}
ApiKeyStore::~ApiKeyStore() = default;

bool ApiKeyStore::SetKey(const std::string& account, const std::string& key) {
  if (account.empty() || key.empty()) {
    return false;
  }
  if (auto* keys = MemoryKeys()) {
    (*keys)[account] = key;
    return true;
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
  if (auto* keys = MemoryKeys()) {
    const auto key = keys->find(account);
    return key == keys->end() ? std::nullopt : std::optional(key->second);
  }
  auto result = crypto::apple::KeychainV2::GetInstance().FindGenericPassword(
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
  if (auto* keys = MemoryKeys()) {
    return keys->erase(account) > 0;
  }
  // KeychainV2 has no delete-by-account helper; delete via a matching query.
  base::apple::ScopedCFTypeRef<CFMutableDictionaryRef> query(
      CFDictionaryCreateMutable(kCFAllocatorDefault, 0,
                                &kCFTypeDictionaryKeyCallBacks,
                                &kCFTypeDictionaryValueCallBacks));
  if (!query) {
    return false;
  }
  base::apple::ScopedCFTypeRef<CFStringRef> service(CFStringCreateWithCString(
      kCFAllocatorDefault, service_name_.c_str(), kCFStringEncodingUTF8));
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
