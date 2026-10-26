// Copyright 2026 The uw Authors. All rights reserved.

#ifndef UW_BROWSER_UPDATE_SPARKLE_UPDATER_H_
#define UW_BROWSER_UPDATE_SPARKLE_UPDATER_H_

#include "base/functional/callback_forward.h"

namespace uw {

// Starts Sparkle in a release, whose bundle carries Sparkle.framework and a
// feed address. Development builds have neither and never check. See ADR 0019.
void StartSparkleUpdater();

// Sparkle has downloaded an update: remember how to install it and let
// Chromium show its "Relaunch to update" prompt.
void OnUpdateDownloaded(base::RepeatingClosure install);

}  // namespace uw

#endif  // UW_BROWSER_UPDATE_SPARKLE_UPDATER_H_
