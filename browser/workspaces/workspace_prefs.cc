// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/browser/workspaces/workspace_prefs.h"

#include "components/prefs/pref_registry_simple.h"

namespace uw {

void RegisterWorkspaceLocalStatePrefs(PrefRegistrySimple* registry) {
  registry->RegisterListPref(kWorkspacesPref);
}

}  // namespace uw
