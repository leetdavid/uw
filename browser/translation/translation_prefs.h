// Copyright 2026 The uw Authors. All rights reserved.

#ifndef UW_BROWSER_TRANSLATION_TRANSLATION_PREFS_H_
#define UW_BROWSER_TRANSLATION_TRANSLATION_PREFS_H_

class PrefRegistrySimple;

namespace user_prefs {
class PrefRegistrySyncable;
}

namespace uw {

// The profile's translation engine: "" for none, "device", "google_cloud", or
// "ai". See docs/plans/page-translation.md.
inline constexpr char kTranslationEnginePref[] = "uw.translation.engine";

// The language model that translates for the AI provider engine: a dict of
// ModelChoice's provider_id, model, format, and effort.
inline constexpr char kTranslationModelPref[] = "uw.translation.model";

// In local state: whether a Google Cloud Translation key is saved. The key
// itself is in the key store, which every profile shares, and this keeps the
// menus from reading it.
inline constexpr char kTranslationGoogleCloudKeySavedPref[] =
    "uw.translation.google_cloud_key_saved";

void RegisterTranslationProfilePrefs(
    user_prefs::PrefRegistrySyncable* registry);
void RegisterTranslationLocalStatePrefs(PrefRegistrySimple* registry);

}  // namespace uw

#endif  // UW_BROWSER_TRANSLATION_TRANSLATION_PREFS_H_
