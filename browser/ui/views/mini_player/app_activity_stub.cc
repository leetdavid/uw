// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/browser/ui/views/mini_player/app_activity.h"

// Whether another app is in front is asked of macOS only so far, so the mini
// player stays docked elsewhere; see docs/plans/windows.md.

namespace uw {

bool IsAppActive() {
  return true;
}

base::CallbackListSubscription ObserveAppActivity(
    base::RepeatingClosure callback) {
  return {};
}

void SetAppActiveForTesting(std::optional<bool> active) {}

}  // namespace uw
