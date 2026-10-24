// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/components/agent/chat_usage.h"

#include "base/json/values_util.h"

namespace uw {
namespace {

int64_t Count(const base::DictValue& dict, std::string_view key) {
  return static_cast<int64_t>(dict.FindDouble(key).value_or(0));
}

}  // namespace

base::DictValue UsageRecordToValue(const UsageRecord& record) {
  base::DictValue value;
  value.Set("kind", record.kind == UsageRecord::Kind::kAgent ? "agent"
                                                              : "command_check");
  value.Set("time", base::TimeToValue(record.time));
  value.Set("model", record.model);
  value.Set("usage", UsageToValue(record.usage));
  if (record.cost) {
    value.Set("cost", *record.cost);
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
  if (!kind || !read || (*kind != "agent" && *kind != "command_check")) {
    return std::nullopt;
  }
  UsageRecord record;
  record.kind = *kind == "agent" ? UsageRecord::Kind::kAgent
                                 : UsageRecord::Kind::kCommandCheck;
  if (const base::Value* time = dict->Find("time")) {
    record.time = base::ValueToTime(*time).value_or(base::Time());
  }
  if (const std::string* model = dict->FindString("model")) {
    record.model = *model;
  }
  record.usage = *read;
  record.cost = dict->FindDouble("cost");
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
