// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/browser/shortcuts/menu_keys.h"

#include "components/prefs/pref_registry_simple.h"

namespace uw {

void RegisterMenuKeysPrefs(PrefRegistrySimple* registry) {
  registry->RegisterDictionaryPref(kMenuKeysPref);
}

}  // namespace uw
