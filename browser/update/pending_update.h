// Copyright 2026 The uw Authors. All rights reserved.

#ifndef UW_BROWSER_UPDATE_PENDING_UPDATE_H_
#define UW_BROWSER_UPDATE_PENDING_UPDATE_H_

#include "base/functional/callback_forward.h"

namespace uw {

// Whether Sparkle has downloaded an update that installs on relaunch. Safe to
// call on any thread.
bool HasPendingUpdate();

// Records an update ready to install. `install` quits uw, installs the update,
// and starts the new uw. It runs again if a quit was cancelled.
void SetPendingUpdate(base::RepeatingClosure install);

// Installs the pending update, if there is one. Returns whether there was.
bool InstallPendingUpdate();

void ClearPendingUpdateForTesting();

}  // namespace uw

#endif  // UW_BROWSER_UPDATE_PENDING_UPDATE_H_
