// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#ifndef UW_BROWSER_AI_PROVIDERS_SCOPED_MEMORY_KEYCHAIN_H_
#define UW_BROWSER_AI_PROVIDERS_SCOPED_MEMORY_KEYCHAIN_H_

#include <memory>

namespace uw {

// Keeps generic passwords in memory while it lives, so a test stores and
// reads API keys without touching the macOS Keychain.
class ScopedMemoryKeychain {
 public:
  ScopedMemoryKeychain();
  ~ScopedMemoryKeychain();

 private:
  class Keychain;
  std::unique_ptr<Keychain> keychain_;
};

}  // namespace uw

#endif  // UW_BROWSER_AI_PROVIDERS_SCOPED_MEMORY_KEYCHAIN_H_
