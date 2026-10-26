// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/components/ai_providers/usage.h"

#include <algorithm>

#include "base/json/json_reader.h"

namespace uw {
namespace {

// A count that JSON may give as a whole or a decimal number.
int64_t Count(const base::DictValue* dict, std::string_view path) {
  const base::Value* value = dict ? dict->FindByDottedPath(path) : nullptr;
  if (!value || !(value->is_int() || value->is_double())) {
    return 0;
  }
  return std::max<int64_t>(0, static_cast<int64_t>(value->GetDouble()));
}

}  // namespace

std::optional<ModelUsage> UsageInAnswer(std::string_view chat_answer) {
  std::optional<base::DictValue> answer =
      base::JSONReader::ReadDict(chat_answer, base::JSON_PARSE_RFC);
  const base::DictValue* usage = answer ? answer->FindDict("usage") : nullptr;
  if (!usage) {
    return std::nullopt;
  }
  ModelUsage read;
  read.input = Count(usage, "prompt_tokens");
  read.output = Count(usage, "completion_tokens");
  read.cache_read = Count(usage, "prompt_tokens_details.cached_tokens");
  if (read.cache_read == 0) {
    read.cache_read = Count(usage, "prompt_cache_hit_tokens");
  }
  read.cache_write = Count(usage, "prompt_tokens_details.cache_write_tokens");
  read.reasoning = Count(usage, "completion_tokens_details.reasoning_tokens");
  if (std::optional<double> cost = usage->FindDouble("cost")) {
    read.cost = *cost;
  }
  if (read.input == 0 && read.output == 0) {
    return std::nullopt;
  }
  return read;
}

std::optional<double> CostOf(const ModelUsage& usage,
                             const std::optional<ModelPrice>& price) {
  if (usage.cost) {
    return usage.cost;
  }
  if (!price) {
    return std::nullopt;
  }
  const int64_t fresh =
      std::max<int64_t>(0, usage.input - usage.cache_read - usage.cache_write);
  const double per_token =
      fresh * price->input +
      usage.cache_read * price->cache_read.value_or(price->input) +
      usage.cache_write * price->cache_write.value_or(price->input) +
      usage.output * price->output;
  return per_token / 1'000'000;
}

base::DictValue UsageToValue(const ModelUsage& usage) {
  base::DictValue value;
  value.Set("input", static_cast<double>(usage.input));
  value.Set("cache_read", static_cast<double>(usage.cache_read));
  value.Set("cache_write", static_cast<double>(usage.cache_write));
  value.Set("output", static_cast<double>(usage.output));
  value.Set("reasoning", static_cast<double>(usage.reasoning));
  if (usage.cost) {
    value.Set("cost", *usage.cost);
  }
  return value;
}

std::optional<ModelUsage> UsageFromValue(const base::DictValue& value) {
  if (!value.Find("input") || !value.Find("output")) {
    return std::nullopt;
  }
  ModelUsage usage;
  usage.input = Count(&value, "input");
  usage.cache_read = Count(&value, "cache_read");
  usage.cache_write = Count(&value, "cache_write");
  usage.output = Count(&value, "output");
  usage.reasoning = Count(&value, "reasoning");
  if (std::optional<double> cost = value.FindDouble("cost")) {
    usage.cost = *cost;
  }
  return usage;
}

}  // namespace uw
