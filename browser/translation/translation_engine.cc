// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/browser/translation/translation_engine.h"

#include <string>

#include "chrome/browser/browser_process.h"
#include "chrome/browser/profiles/profile.h"
#include "components/prefs/pref_service.h"
#include "uw/browser/ai_providers/models.h"
#include "uw/browser/translation/ai_translator.h"
#include "uw/browser/translation/translation_prefs.h"

namespace uw {

TranslationEngine GetTranslationEngine(const PrefService& prefs) {
  const std::string& name = prefs.GetString(kTranslationEnginePref);
  for (TranslationEngine engine :
       {TranslationEngine::kDevice, TranslationEngine::kGoogleCloud,
        TranslationEngine::kAi}) {
    if (name == TranslationEngineName(engine)) {
      return engine;
    }
  }
  return TranslationEngine::kNone;
}

void SetTranslationEngine(PrefService& prefs, TranslationEngine engine) {
  prefs.SetString(kTranslationEnginePref,
                  std::string(TranslationEngineName(engine)));
}

std::string_view TranslationEngineName(TranslationEngine engine) {
  switch (engine) {
    case TranslationEngine::kNone:
      return "";
    case TranslationEngine::kDevice:
      return "device";
    case TranslationEngine::kGoogleCloud:
      return "google_cloud";
    case TranslationEngine::kAi:
      return "ai";
  }
}

bool IsTranslationEngineSetUp(Profile* profile, TranslationEngine engine) {
  switch (engine) {
    case TranslationEngine::kNone:
      return false;
    case TranslationEngine::kDevice:
      return true;
    case TranslationEngine::kGoogleCloud:
      return g_browser_process->local_state()->GetBoolean(
          kTranslationGoogleCloudKeySavedPref);
    case TranslationEngine::kAi: {
      const PrefService& prefs = *profile->GetOriginalProfile()->GetPrefs();
      std::optional<ModelChoice> choice = ReadTranslationModel(prefs);
      return choice && IsComplete(ConfigForChoice(prefs, *choice));
    }
  }
}

}  // namespace uw
