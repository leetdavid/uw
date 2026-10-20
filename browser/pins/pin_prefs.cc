// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/browser/pins/pin_prefs.h"

#include "components/pref_registry/pref_registry_syncable.h"

namespace uw {

void RegisterPinProfilePrefs(user_prefs::PrefRegistrySyncable* registry) {
  registry->RegisterListPref(kPinsPref);
}

}  // namespace uw
