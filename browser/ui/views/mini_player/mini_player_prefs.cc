// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/browser/ui/views/mini_player/mini_player_prefs.h"

#include "components/prefs/pref_registry_simple.h"
#include "components/prefs/pref_service.h"

namespace uw {

void RegisterMiniPlayerLocalStatePrefs(PrefRegistrySimple* registry) {
  registry->RegisterIntegerPref(kMiniPlayerModePref,
                                static_cast<int>(MiniPlayerMode::kDock));
  registry->RegisterBooleanPref(kMiniPlayerOnTabSwitchPref, true);
  registry->RegisterBooleanPref(kMiniPlayerFloatInBackgroundPref, true);
}

MiniPlayerMode MiniPlayerModeOf(const PrefService* local_state) {
  const int mode = local_state ? local_state->GetInteger(kMiniPlayerModePref)
                               : static_cast<int>(MiniPlayerMode::kDock);
  return mode >= 0 && mode <= static_cast<int>(MiniPlayerMode::kMaxValue)
             ? static_cast<MiniPlayerMode>(mode)
             : MiniPlayerMode::kDock;
}

}  // namespace uw
