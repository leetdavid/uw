// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/browser/ui/look/page_space_prefs.h"

#include "components/prefs/pref_registry_simple.h"

namespace uw {

void RegisterPageSpaceLocalStatePrefs(PrefRegistrySimple* registry) {
  registry->RegisterBooleanPref(kPageSpacePref, true);
}

}  // namespace uw
