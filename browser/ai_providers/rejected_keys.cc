// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/browser/ai_providers/rejected_keys.h"

#include <utility>

#include "crypto/sha2.h"

namespace uw {

// static
RejectedKeys& RejectedKeys::Get() {
  static base::NoDestructor<RejectedKeys> keys;
  return *keys;
}

RejectedKeys::RejectedKeys() = default;
RejectedKeys::~RejectedKeys() = default;

// static
std::string RejectedKeys::HashOf(std::string_view key) {
  return crypto::SHA256HashString(key);
}

void RejectedKeys::Heard(const std::string& key_hash,
                         bool ok,
                         int http_status) {
  if (key_hash.empty()) {
    return;
  }
  const bool changed = http_status == 401 || http_status == 403
                           ? rejected_.insert(key_hash).second
                           : ok && rejected_.erase(key_hash) > 0;
  if (changed) {
    changed_.Notify();
  }
}

bool RejectedKeys::Rejected(std::string_view key) const {
  return !key.empty() && rejected_.contains(HashOf(key));
}

base::CallbackListSubscription RejectedKeys::Observe(
    base::RepeatingClosure callback) {
  return changed_.Add(std::move(callback));
}

}  // namespace uw
