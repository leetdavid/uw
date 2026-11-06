// Copyright 2026 The uw Authors. All rights reserved.

#ifndef UW_BROWSER_TRANSLATION_AI_TRANSLATOR_H_
#define UW_BROWSER_TRANSLATION_AI_TRANSLATOR_H_

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "uw/browser/ai_providers/remote_model_client.h"
#include "uw/browser/translation/segment_translator.h"
#include "uw/components/ai_providers/providers.h"

class PrefService;
class Profile;

namespace uw {

// The model picked to translate, or else the model that names tasks, at its
// provider's default effort.
std::optional<ModelChoice> ReadTranslationModel(const PrefService& prefs);
void SetTranslationModel(PrefService& prefs, const ModelChoice& choice);

// A chat completions request that has the model translate `segments` from
// `source` to `target`, keeping each <a i=N> tag. Exposed for tests.
std::string BuildTranslationRequestBody(
    std::string_view model,
    std::string_view source,
    std::string_view target,
    const std::vector<std::string>& segments);

// The translations in a model's answer, one per segment, or nullopt when the
// answer is not a list of `count` strings. Exposed for tests.
std::optional<std::vector<std::string>> ParseTranslationResponse(
    std::string_view body,
    size_t count);

// Translates with a language model from one of the user's providers, picked
// in the translation settings. A batch goes in requests of a few thousand
// characters, so a slow or small model can take each one.
class AiTranslator : public SegmentTranslator {
 public:
  // The most text one request carries. A segment longer than this goes alone.
  static constexpr size_t kMaxRequestChars = 3000;

  explicit AiTranslator(Profile* profile);
  ~AiTranslator() override;

  // SegmentTranslator:
  void Translate(std::vector<std::string> segments,
                 const std::string& source_language,
                 const std::string& target_language,
                 ResultCallback callback) override;

 private:
  struct Job;

  void OnAnswer(Job* job,
                size_t first,
                size_t count,
                RemoteModelClient* client,
                RemoteModelClient::Outcome outcome);
  void Finish(Job* job);

  raw_ptr<Profile> profile_;
  std::vector<std::unique_ptr<Job>> jobs_;
  base::WeakPtrFactory<AiTranslator> weak_factory_{this};
};

}  // namespace uw

#endif  // UW_BROWSER_TRANSLATION_AI_TRANSLATOR_H_
