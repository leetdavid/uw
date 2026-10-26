// Copyright 2026 The uw Authors. All rights reserved.

#ifndef UW_BROWSER_UI_VIEWS_MINI_PLAYER_APP_ACTIVITY_H_
#define UW_BROWSER_UI_VIEWS_MINI_PLAYER_APP_ACTIVITY_H_

#include <optional>

#include "base/callback_list.h"
#include "base/functional/callback.h"

namespace uw {

// True while uw is the app in front, as macOS says.
bool IsAppActive();

// Runs `callback` each time uw comes to the front or another app does.
base::CallbackListSubscription ObserveAppActivity(
    base::RepeatingClosure callback);

// Makes IsAppActive() answer `active` and tells the observers, for tests,
// whose app is rarely in front. std::nullopt asks macOS again.
void SetAppActiveForTesting(std::optional<bool> active);

}  // namespace uw

#endif  // UW_BROWSER_UI_VIEWS_MINI_PLAYER_APP_ACTIVITY_H_
