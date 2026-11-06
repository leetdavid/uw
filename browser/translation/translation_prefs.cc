// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/browser/translation/translation_prefs.h"

#include <string>

#include "components/pref_registry/pref_registry_syncable.h"

namespace uw {

void RegisterTranslationProfilePrefs(
    user_prefs::PrefRegistrySyncable* registry) {
  registry->RegisterStringPref(kTranslationEnginePref, std::string());
}

}  // namespace uw
