// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/browser/little_window/little_window_prefs.h"

#include "components/prefs/pref_registry_simple.h"

namespace uw {

void RegisterLittleWindowLocalStatePrefs(PrefRegistrySimple* registry) {
  registry->RegisterBooleanPref(kLittleWindowKeyInUwPref, true);
  registry->RegisterBooleanPref(kLittleWindowKeyFromAppsPref, true);
  registry->RegisterBooleanPref(kLittleWindowLinksPref, false);
  registry->RegisterIntegerPref(kLittleWindowCloseAfterPref,
                                kLittleWindowCloseAfterDefault);
}

}  // namespace uw
