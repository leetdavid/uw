// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/browser/ai_providers/scoped_memory_keychain.h"

// Off the Mac, ApiKeyStore keeps keys in memory already.

namespace uw {

class ScopedMemoryKeychain::Keychain {};

ScopedMemoryKeychain::ScopedMemoryKeychain() = default;

ScopedMemoryKeychain::~ScopedMemoryKeychain() = default;

}  // namespace uw
