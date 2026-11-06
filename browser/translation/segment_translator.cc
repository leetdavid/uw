// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/browser/translation/segment_translator.h"

#include <utility>

namespace uw {

SegmentTranslator::Result::Result() = default;
SegmentTranslator::Result::Result(const Result&) = default;
SegmentTranslator::Result::Result(Result&&) = default;
SegmentTranslator::Result& SegmentTranslator::Result::operator=(const Result&) =
    default;
SegmentTranslator::Result& SegmentTranslator::Result::operator=(Result&&) =
    default;
SegmentTranslator::Result::~Result() = default;

// static
SegmentTranslator::Result SegmentTranslator::Result::Unsupported(
    std::string language) {
  Result result;
  result.problem = Problem::kUnsupportedLanguage;
  result.language = std::move(language);
  return result;
}

// static
SegmentTranslator::Result SegmentTranslator::Result::Failed(
    std::string message) {
  Result result;
  result.problem = Problem::kFailed;
  result.message = std::move(message);
  return result;
}

}  // namespace uw
