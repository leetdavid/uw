// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/browser/pins/pin_prefs.h"

#include "components/pref_registry/pref_registry_syncable.h"

namespace uw {

void RegisterPinProfilePrefs(user_prefs::PrefRegistrySyncable* registry) {
  registry->RegisterListPref(kPinsPref);
}

}  // namespace uw
