// Copyright 2026 The uw Authors. All rights reserved.

#ifndef UW_COMPONENTS_AI_PROVIDERS_USAGE_H_
#define UW_COMPONENTS_AI_PROVIDERS_USAGE_H_

#include <cstdint>
#include <optional>
#include <string_view>

#include "base/values.h"

namespace uw {

// A model's price in dollars per million tokens, as models.dev lists it. A
// cache price it leaves out is the input price.
struct ModelPrice {
  double input = 0;
  double output = 0;
  std::optional<double> cache_read;
  std::optional<double> cache_write;

  bool operator==(const ModelPrice&) const = default;
};

// What one request to a language model used, as its provider reported it.
// See docs/plans/context-and-cost.md.
struct ModelUsage {
  // Every input token, the cached ones included.
  int64_t input = 0;
  int64_t cache_read = 0;
  int64_t cache_write = 0;
  // Every output token, thinking included.
  int64_t output = 0;
  int64_t reasoning = 0;
  // What the provider said the request cost, in dollars, when it said.
  std::optional<double> cost;

  bool operator==(const ModelUsage&) const = default;
};

// The usage a chat-completions answer reports, which the formats' answers
// are turned into. DeepSeek's cache fields and OpenRouter's own cost count.
// Nothing for an answer that reports none.
std::optional<ModelUsage> UsageInAnswer(std::string_view chat_answer);

// What `usage` cost in dollars: the provider's own figure when it gave one,
// else priced with `price`. Nothing when there is neither.
std::optional<double> CostOf(const ModelUsage& usage,
                             const std::optional<ModelPrice>& price);

base::DictValue UsageToValue(const ModelUsage& usage);
std::optional<ModelUsage> UsageFromValue(const base::DictValue& value);

}  // namespace uw

#endif  // UW_COMPONENTS_AI_PROVIDERS_USAGE_H_
