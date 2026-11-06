// Copyright 2026 The uw Authors. All rights reserved.

#ifndef UW_BROWSER_TRANSLATION_SEGMENT_TRANSLATOR_H_
#define UW_BROWSER_TRANSLATION_SEGMENT_TRANSLATOR_H_

#include <string>
#include <vector>

#include "base/functional/callback.h"

namespace uw {

// A translation engine as the page translator uses it: it translates a batch
// of the page's text segments, each a run of text in which the page's
// elements are written as <a i=N>, and keeps the elements.
class SegmentTranslator {
 public:
  struct Result {
    enum class Problem {
      kNone,
      // The engine does not know `language`.
      kUnsupportedLanguage,
      // The engine or its service failed; `message` says how, in its words
      // when it gave any.
      kFailed,
    };

    Result();
    Result(const Result&);
    Result(Result&&);
    Result& operator=(const Result&);
    Result& operator=(Result&&);
    ~Result();

    static Result Unsupported(std::string language);
    static Result Failed(std::string message);

    // One translation per segment when there is no problem.
    std::vector<std::string> translations;
    Problem problem = Problem::kNone;
    std::string language;
    std::string message;
  };
  using ResultCallback = base::OnceCallback<void(Result)>;

  virtual ~SegmentTranslator() = default;

  virtual void Translate(std::vector<std::string> segments,
                         const std::string& source_language,
                         const std::string& target_language,
                         ResultCallback callback) = 0;
};

}  // namespace uw

#endif  // UW_BROWSER_TRANSLATION_SEGMENT_TRANSLATOR_H_
