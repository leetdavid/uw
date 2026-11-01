// Copyright 2026 The uw Authors. All rights reserved.

// Non-macOS fallback for ApiKeyStore. The first release targets macOS
// desktop with Keychain storage; other platforms keep keys in process memory
// so builds link and Ollama (keyless) works everywhere. Secrets never leave
// the process and never touch disk or prefs.

#include "uw/browser/ai_providers/api_key_store.h"

#include <map>
#include <mutex>
#include <optional>
#include <string>

#include "base/no_destructor.h"

namespace uw {
namespace {

struct Keys {
  std::mutex mutex;
  std::map<std::string, std::string> by_account;
};

Keys& Store() {
  static base::NoDestructor<Keys> keys;
  return *keys;
}

}  // namespace

ApiKeyStore::ApiKeyStore() = default;
ApiKeyStore::~ApiKeyStore() = default;

bool ApiKeyStore::SetKey(const std::string& account, const std::string& key) {
  if (account.empty() || key.empty()) {
    return false;
  }
  Keys& keys = Store();
  std::lock_guard<std::mutex> lock(keys.mutex);
  keys.by_account[account] = key;
  return true;
}

std::optional<std::string> ApiKeyStore::GetKey(const std::string& account) {
  Keys& keys = Store();
  std::lock_guard<std::mutex> lock(keys.mutex);
  auto it = keys.by_account.find(account);
  if (it == keys.by_account.end()) {
    return std::nullopt;
  }
  return it->second;
}

bool ApiKeyStore::DeleteKey(const std::string& account) {
  Keys& keys = Store();
  std::lock_guard<std::mutex> lock(keys.mutex);
  return keys.by_account.erase(account) > 0;
}

}  // namespace uw
