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

std::mutex& KeyMutex() {
  static base::NoDestructor<std::mutex> mutex;
  return *mutex;
}

std::map<std::string, std::string>& KeyMap() {
  static base::NoDestructor<std::map<std::string, std::string>> keys;
  return *keys;
}

}  // namespace

ApiKeyStore::ApiKeyStore() = default;
ApiKeyStore::~ApiKeyStore() = default;

bool ApiKeyStore::SetKey(const std::string& account, const std::string& key) {
  if (account.empty() || key.empty()) {
    return false;
  }
  std::lock_guard<std::mutex> lock(KeyMutex());
  KeyMap()[account] = key;
  return true;
}

std::optional<std::string> ApiKeyStore::GetKey(const std::string& account) {
  std::lock_guard<std::mutex> lock(KeyMutex());
  auto it = KeyMap().find(account);
  if (it == KeyMap().end()) {
    return std::nullopt;
  }
  return it->second;
}

bool ApiKeyStore::DeleteKey(const std::string& account) {
  std::lock_guard<std::mutex> lock(KeyMutex());
  return KeyMap().erase(account) > 0;
}

}  // namespace uw
