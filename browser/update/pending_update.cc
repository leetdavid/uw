// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/browser/update/pending_update.h"

#include <atomic>
#include <utility>

#include "base/functional/callback.h"
#include "base/no_destructor.h"

namespace uw {
namespace {

std::atomic<bool> g_pending{false};

base::RepeatingClosure& Install() {
  static base::NoDestructor<base::RepeatingClosure> install;
  return *install;
}

}  // namespace

bool HasPendingUpdate() {
  return g_pending.load();
}

void SetPendingUpdate(base::RepeatingClosure install) {
  Install() = std::move(install);
  g_pending.store(true);
}

bool InstallPendingUpdate() {
  if (!g_pending.load()) {
    return false;
  }
  Install().Run();
  return true;
}

void ClearPendingUpdateForTesting() {
  g_pending.store(false);
  Install().Reset();
}

}  // namespace uw
