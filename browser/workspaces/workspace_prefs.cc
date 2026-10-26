// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/browser/workspaces/workspace_prefs.h"

#include "components/prefs/pref_registry_simple.h"

namespace uw {

void RegisterWorkspaceLocalStatePrefs(PrefRegistrySimple* registry) {
  registry->RegisterListPref(kWorkspacesPref);
}

}  // namespace uw
