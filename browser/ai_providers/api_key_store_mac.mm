// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/browser/ai_providers/api_key_store.h"

#include <Security/Security.h>

#include <map>
#include <string_view>
#include <vector>

#include "base/apple/bridging.h"
#include "base/apple/foundation_util.h"
#include "base/apple/scoped_cftyperef.h"
#include "base/command_line.h"
#include "base/containers/span.h"
#include "base/no_destructor.h"
#include "base/strings/sys_string_conversions.h"
#include "components/os_crypt/common/os_crypt_switches.h"
#include "crypto/apple/keychain_v2.h"
#include "uw/components/ai_providers/providers.h"

namespace uw {
namespace {

// The installed uw's keys. uw dev keeps its own, so neither app asks for the
// other's or replaces them. See docs/adr/0028-local-builds-are-uw-dev.md.
constexpr char kInstalledUwService[] = "uw-ai";
constexpr const char* kService =
    BUILDFLAG(UW_DEV) ? "uw-ai-dev" : kInstalledUwService;

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

#if BUILDFLAG(UW_DEV)
// The accounts with a key under `service`. Reads no key, so macOS asks
// nothing.
std::vector<std::string> AccountsIn(std::string_view service) {
  NSDictionary* query = @{
    base::apple::CFToNSPtrCast(kSecClass) :
        base::apple::CFToNSPtrCast(kSecClassGenericPassword),
    base::apple::CFToNSPtrCast(kSecAttrService) :
        base::SysUTF8ToNSString(service),
    base::apple::CFToNSPtrCast(kSecMatchLimit) :
        base::apple::CFToNSPtrCast(kSecMatchLimitAll),
    base::apple::CFToNSPtrCast(kSecReturnAttributes) : @YES,
  };
  base::apple::ScopedCFTypeRef<CFTypeRef> items;
  std::vector<std::string> accounts;
  if (crypto::apple::KeychainV2::GetInstance().ItemCopyMatching(
          base::apple::NSToCFPtrCast(query), items.InitializeInto()) !=
      errSecSuccess) {
    return accounts;
  }
  NSArray* found =
      base::apple::CFToNSPtrCast(base::apple::CFCast<CFArrayRef>(items.get()));
  for (NSDictionary* item in found) {
    NSString* account = base::apple::ObjCCast<NSString>(
        item[base::apple::CFToNSPtrCast(kSecAttrAccount)]);
    if (account) {
      accounts.push_back(base::SysNSStringToUTF8(account));
    }
  }
  return accounts;
}
#endif  // BUILDFLAG(UW_DEV)

}  // namespace

ApiKeyStore::ApiKeyStore() : service_name_(kService) {}
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

#if BUILDFLAG(UW_DEV)
void CopyModelKeysFromInstalledUw() {
  if (MemoryKeys() || !AccountsIn(kService).empty()) {
    return;
  }
  crypto::apple::KeychainV2& keychain =
      crypto::apple::KeychainV2::GetInstance();
  const std::string model_keys = KeyAccountFor("");
  for (const std::string& account : AccountsIn(kInstalledUwService)) {
    if (!account.starts_with(model_keys)) {
      continue;
    }
    // macOS asks for the login password here.
    const auto key = keychain.FindGenericPassword(kInstalledUwService, account);
    if (key.has_value()) {
      keychain.AddGenericPassword(kService, account, *key);
    }
  }
}
#endif  // BUILDFLAG(UW_DEV)

}  // namespace uw
