// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/browser/ui/views/mini_player/mini_player_prefs.h"

#include "components/prefs/pref_registry_simple.h"

namespace uw {

void RegisterMiniPlayerLocalStatePrefs(PrefRegistrySimple* registry) {
  registry->RegisterBooleanPref(kMiniPlayerInSidebarPref, true);
  registry->RegisterBooleanPref(kMiniPlayerOnTabSwitchPref, true);
  registry->RegisterBooleanPref(kMiniPlayerFloatInBackgroundPref, true);
}

}  // namespace uw
