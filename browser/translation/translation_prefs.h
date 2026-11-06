// Copyright 2026 The uw Authors. All rights reserved.

#ifndef UW_BROWSER_TRANSLATION_TRANSLATION_PREFS_H_
#define UW_BROWSER_TRANSLATION_TRANSLATION_PREFS_H_

namespace user_prefs {
class PrefRegistrySyncable;
}

namespace uw {

// The profile's translation engine: "" for none, "device", "google_cloud", or
// "ai". See docs/plans/page-translation.md.
inline constexpr char kTranslationEnginePref[] = "uw.translation.engine";

void RegisterTranslationProfilePrefs(
    user_prefs::PrefRegistrySyncable* registry);

}  // namespace uw

#endif  // UW_BROWSER_TRANSLATION_TRANSLATION_PREFS_H_
