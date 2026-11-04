// Copyright 2026 The uw Authors. All rights reserved.

#ifndef UW_BROWSER_UPDATE_UPDATE_CHECK_H_
#define UW_BROWSER_UPDATE_UPDATE_CHECK_H_

#include "base/callback_list.h"
#include "base/functional/callback_forward.h"

namespace uw {

// Where uw's own updater is: Sparkle on the Mac, uw's fetcher on Windows.
// About uw shows it. See docs/plans/distribution.md, "Updates".
enum class UpdateState {
  kIdle,
  kChecking,
  kDownloading,
  // A newer uw waits for a relaunch.
  kReady,
  kUpToDate,
  kFailed,
};

// Set by the updater once it runs. `check_now` starts a check unless one is
// under way. A build without an updater never sets it.
void SetUpdateChecker(base::RepeatingClosure check_now);
bool HasUpdateChecker();
void CheckForUpdateNow();

UpdateState CurrentUpdateState();
void ReportUpdateState(UpdateState state);
base::CallbackListSubscription WatchUpdateState(
    base::RepeatingCallback<void(UpdateState)> watcher);

void ResetUpdateCheckForTesting();

}  // namespace uw

#endif  // UW_BROWSER_UPDATE_UPDATE_CHECK_H_
