// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/components/agent/chat_usage.h"

#include <algorithm>

#include "base/json/values_util.h"

namespace uw {
namespace {

int64_t Count(const base::DictValue& dict, std::string_view key) {
  return static_cast<int64_t>(dict.FindDouble(key).value_or(0));
}

const char* KindName(UsageRecord::Kind kind) {
  switch (kind) {
    case UsageRecord::Kind::kAgent:
      return "agent";
    case UsageRecord::Kind::kCommandCheck:
      return "command_check";
    case UsageRecord::Kind::kMemoryCheck:
      return "memory_check";
  }
}

std::optional<UsageRecord::Kind> KindNamed(std::string_view name) {
  for (UsageRecord::Kind kind :
       {UsageRecord::Kind::kAgent, UsageRecord::Kind::kCommandCheck,
        UsageRecord::Kind::kMemoryCheck}) {
    if (name == KindName(kind)) {
      return kind;
    }
  }
  return std::nullopt;
}

}  // namespace

base::DictValue UsageRecordToValue(const UsageRecord& record) {
  base::DictValue value;
  value.Set("kind", KindName(record.kind));
  value.Set("time", base::TimeToValue(record.time));
  value.Set("model", record.model);
  value.Set("usage", UsageToValue(record.usage));
  if (record.cost) {
    value.Set("cost", *record.cost);
  }
  if (record.price) {
    base::DictValue price =
        base::DictValue()
            .Set("input", record.price->input)
            .Set("output", record.price->output);
    if (record.price->cache_read) {
      price.Set("cache_read", *record.price->cache_read);
    }
    if (record.price->cache_write) {
      price.Set("cache_write", *record.price->cache_write);
    }
    value.Set("price", std::move(price));
  }
  if (record.kind == UsageRecord::Kind::kAgent) {
    value.Set("parts", base::DictValue()
                           .Set("instructions",
                                static_cast<double>(record.parts.instructions))
                           .Set("conversation",
                                static_cast<double>(record.parts.conversation))
                           .Set("results",
                                static_cast<double>(record.parts.results))
                           .Set("images",
                                static_cast<double>(record.parts.images)));
    value.Set("max_length", static_cast<double>(record.max_length));
    value.Set("input_limit", static_cast<double>(record.input_limit));
    value.Set("made_room", record.made_room);
    value.Set("message", record.message);
  }
  return value;
}

std::optional<UsageRecord> UsageRecordFromValue(const base::Value& value) {
  const base::DictValue* dict = value.GetIfDict();
  const std::string* kind = dict ? dict->FindString("kind") : nullptr;
  const base::DictValue* usage = dict ? dict->FindDict("usage") : nullptr;
  std::optional<ModelUsage> read =
      usage ? UsageFromValue(*usage) : std::nullopt;
  const std::optional<UsageRecord::Kind> named =
      kind ? KindNamed(*kind) : std::nullopt;
  if (!named || !read) {
    return std::nullopt;
  }
  UsageRecord record;
  record.kind = *named;
  if (const base::Value* time = dict->Find("time")) {
    record.time = base::ValueToTime(*time).value_or(base::Time());
  }
  if (const std::string* model = dict->FindString("model")) {
    record.model = *model;
  }
  record.usage = *read;
  record.cost = dict->FindDouble("cost");
  if (const base::DictValue* price = dict->FindDict("price")) {
    record.price = ModelPrice{.input = price->FindDouble("input").value_or(0),
                              .output = price->FindDouble("output").value_or(0),
                              .cache_read = price->FindDouble("cache_read"),
                              .cache_write = price->FindDouble("cache_write")};
  }
  if (const base::DictValue* parts = dict->FindDict("parts")) {
    record.parts = {.instructions = Count(*parts, "instructions"),
                    .conversation = Count(*parts, "conversation"),
                    .results = Count(*parts, "results"),
                    .images = Count(*parts, "images")};
  }
  record.max_length = Count(*dict, "max_length");
  record.input_limit = Count(*dict, "input_limit");
  record.made_room = dict->FindBool("made_room").value_or(false);
  record.message = dict->FindInt("message").value_or(0);
  return record;
}

UsageSummary::UsageSummary() = default;
UsageSummary::UsageSummary(const UsageSummary&) = default;
UsageSummary& UsageSummary::operator=(const UsageSummary&) = default;
UsageSummary::~UsageSummary() = default;

UsageSummary Summarize(const std::vector<UsageRecord>& records,
                       const std::vector<UsageTotal>& runs) {
  UsageSummary summary;
  int message = 0;
  const auto spend = [&](const UsageRecord& record) {
    if (record.cost) {
      summary.total += *record.cost;
    } else {
      summary.priced = false;
    }
    if (record.cost && !record.price) {
      summary.split = false;
    }
    if (summary.messages.empty() ||
        summary.messages.back().message != message) {
      summary.messages.push_back({.message = message});
    }
    summary.messages.back().cost += record.cost.value_or(0);
  };
  for (const UsageRecord& record : records) {
    const ModelUsage& usage = record.usage;
    if (record.kind == UsageRecord::Kind::kCommandCheck) {
      ++summary.check_count;
      summary.checks.tokens += usage.input + usage.output;
      summary.checks.cost += record.cost.value_or(0);
      spend(record);
      continue;
    }
    if (record.kind == UsageRecord::Kind::kMemoryCheck) {
      ++summary.memory_check_count;
      summary.memory_checks.tokens += usage.input + usage.output;
      summary.memory_checks.cost += record.cost.value_or(0);
      spend(record);
      continue;
    }
    message = std::max(message, record.message);
    ++summary.agent_requests;
    // The request's own tokens per character turn uw's limit in characters
    // into tokens.
    const int64_t characters = record.parts.Total();
    const double per_character =
        characters > 0 ? static_cast<double>(usage.input) / characters : 0;
    const int64_t room =
        static_cast<int64_t>(record.max_length * per_character);
    const bool model_first =
        record.input_limit > 0 && (room == 0 || record.input_limit < room);
    const int64_t limit = model_first ? record.input_limit : room;
    summary.per_request.push_back(usage.input);
    summary.limits.push_back(limit);
    if (record.made_room) {
      summary.made_room.push_back(summary.agent_requests);
    }
    summary.tokens = usage.input;
    summary.limit = limit;
    summary.model_limit = model_first;
    summary.parts = {
        .instructions =
            static_cast<int64_t>(record.parts.instructions * per_character),
        .conversation =
            static_cast<int64_t>(record.parts.conversation * per_character),
        .results = static_cast<int64_t>(record.parts.results * per_character),
        .images = static_cast<int64_t>(record.parts.images * per_character)};

    const int64_t fresh = std::max<int64_t>(
        0, usage.input - usage.cache_read - usage.cache_write);
    summary.fresh.tokens += fresh;
    summary.cache_read.tokens += usage.cache_read;
    summary.cache_write.tokens += usage.cache_write;
    summary.output.tokens += usage.output;
    if (record.price) {
      const ModelPrice& price = *record.price;
      const double read = price.cache_read.value_or(price.input);
      const double write = price.cache_write.value_or(price.input);
      const double listed = (fresh * price.input + usage.cache_read * read +
                             usage.cache_write * write +
                             usage.output * price.output) /
                            1'000'000;
      // A provider's own cost is split as its list price would be.
      const double scale = record.cost && listed > 0 ? *record.cost / listed
                                                      : 1;
      summary.fresh.cost += fresh * price.input / 1'000'000 * scale;
      summary.cache_read.cost += usage.cache_read * read / 1'000'000 * scale;
      summary.cache_write.cost +=
          usage.cache_write * write / 1'000'000 * scale;
      summary.output.cost += usage.output * price.output / 1'000'000 * scale;
      summary.cache_saved += (usage.cache_read * (price.input - read) -
                              usage.cache_write * (write - price.input)) /
                             1'000'000 * scale;
    }
    spend(record);
    ++summary.messages.back().requests;
  }
  for (const UsageTotal& run : runs) {
    ++summary.run_count;
    summary.runs.tokens += run.usage.input + run.usage.output;
    summary.runs.cost += run.cost;
    summary.total += run.cost;
    summary.priced = summary.priced && run.priced;
  }
  summary.cache_saved = std::max(0.0, summary.cache_saved);
  return summary;
}

UsageTotal TotalOf(const std::vector<UsageRecord>& records) {
  UsageTotal total;
  for (const UsageRecord& record : records) {
    total.usage.input += record.usage.input;
    total.usage.cache_read += record.usage.cache_read;
    total.usage.cache_write += record.usage.cache_write;
    total.usage.output += record.usage.output;
    total.usage.reasoning += record.usage.reasoning;
    if (record.cost) {
      total.cost += *record.cost;
    } else {
      total.priced = false;
    }
  }
  return total;
}

}  // namespace uw
