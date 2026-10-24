// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/browser/ui/glass/glass_prefs.h"

#include "components/prefs/pref_registry_simple.h"

namespace uw {

void RegisterGlassLocalStatePrefs(PrefRegistrySimple* registry) {
  registry->RegisterBooleanPref(kGlassBehindPref, true);
}

}  // namespace uw
