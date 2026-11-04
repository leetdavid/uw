// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/browser/update/update_check.h"

#include <utility>

#include "base/functional/callback.h"
#include "base/no_destructor.h"

namespace uw {
namespace {

struct Check {
  base::RepeatingClosure check_now;
  UpdateState state = UpdateState::kIdle;
  base::RepeatingCallbackList<void(UpdateState)> watchers;
};

Check& Get() {
  static base::NoDestructor<Check> check;
  return *check;
}

}  // namespace

void SetUpdateChecker(base::RepeatingClosure check_now) {
  Get().check_now = std::move(check_now);
}

bool HasUpdateChecker() {
  return !Get().check_now.is_null();
}

void CheckForUpdateNow() {
  if (HasUpdateChecker()) {
    Get().check_now.Run();
  }
}

UpdateState CurrentUpdateState() {
  return Get().state;
}

void ReportUpdateState(UpdateState state) {
  Get().state = state;
  Get().watchers.Notify(state);
}

base::CallbackListSubscription WatchUpdateState(
    base::RepeatingCallback<void(UpdateState)> watcher) {
  return Get().watchers.Add(std::move(watcher));
}

void ResetUpdateCheckForTesting() {
  Get().check_now.Reset();
  Get().state = UpdateState::kIdle;
}

}  // namespace uw
