// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/browser/translation/translation_prefs.h"

#include <string>

#include "components/pref_registry/pref_registry_syncable.h"
#include "components/prefs/pref_registry_simple.h"

namespace uw {

void RegisterTranslationProfilePrefs(
    user_prefs::PrefRegistrySyncable* registry) {
  registry->RegisterStringPref(kTranslationEnginePref, std::string());
  registry->RegisterDictionaryPref(kTranslationModelPref);
}

void RegisterTranslationLocalStatePrefs(PrefRegistrySimple* registry) {
  registry->RegisterBooleanPref(kTranslationGoogleCloudKeySavedPref, false);
}

}  // namespace uw
