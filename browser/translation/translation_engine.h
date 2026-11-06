// Copyright 2026 The uw Authors. All rights reserved.

#ifndef UW_BROWSER_TRANSLATION_TRANSLATION_ENGINE_H_
#define UW_BROWSER_TRANSLATION_TRANSLATION_ENGINE_H_

#include <string_view>

class PrefService;
class Profile;

namespace uw {

// What translates a page. See "Translation engine" in docs/CONTEXT.md.
enum class TranslationEngine {
  kNone,
  kDevice,
  kGoogleCloud,
  kAi,
};

TranslationEngine GetTranslationEngine(const PrefService& prefs);
void SetTranslationEngine(PrefService& prefs, TranslationEngine engine);
std::string_view TranslationEngineName(TranslationEngine engine);

// Whether `engine` can translate for `profile` now: this device always, Google
// Cloud Translation once its key is saved, and an AI provider once its model
// is picked.
bool IsTranslationEngineSetUp(Profile* profile, TranslationEngine engine);

}  // namespace uw

#endif  // UW_BROWSER_TRANSLATION_TRANSLATION_ENGINE_H_
