// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#ifndef UW_COMPONENTS_AGENT_CHAT_USAGE_H_
#define UW_COMPONENTS_AGENT_CHAT_USAGE_H_

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "base/time/time.h"
#include "base/values.h"
#include "uw/components/ai_providers/usage.h"

namespace uw {

// How a request's characters split, each image counted as
// AgentContext::kImageLength.
struct RequestParts {
  // The agent's instructions and the tools it is offered.
  int64_t instructions = 0;
  // The user's and the model's messages.
  int64_t conversation = 0;
  // Tool results, page readings among them.
  int64_t results = 0;
  int64_t images = 0;

  int64_t Total() const {
    return instructions + conversation + results + images;
  }
  bool operator==(const RequestParts&) const = default;
};

// One model request a chat made, with what it used and cost. See
// docs/plans/context-and-cost.md.
struct UsageRecord {
  enum class Kind {
    // The agent's own request.
    kAgent,
    // The command check before one of its terminal commands.
    kCommandCheck,
  };

  Kind kind = Kind::kAgent;
  base::Time time;
  std::string model;
  ModelUsage usage;
  // Nothing when the model has no price.
  std::optional<double> cost;

  // For the agent's requests: the request's characters by part, the most
  // characters uw let it reach, the model's own input limit in tokens (0 when
  // unknown), whether uw made room in the context before sending it, and
  // which of the user's messages it answers, counting from 1.
  RequestParts parts;
  int64_t max_length = 0;
  int64_t input_limit = 0;
  bool made_room = false;
  int message = 0;

  bool operator==(const UsageRecord&) const = default;
};

base::DictValue UsageRecordToValue(const UsageRecord& record);
std::optional<UsageRecord> UsageRecordFromValue(const base::Value& value);

// What a set of requests used in all, and what they cost. `priced` is false
// when one of them had no price, so `cost` leaves it out.
struct UsageTotal {
  ModelUsage usage;
  double cost = 0;
  bool priced = true;
};
UsageTotal TotalOf(const std::vector<UsageRecord>& records);

}  // namespace uw

#endif  // UW_COMPONENTS_AGENT_CHAT_USAGE_H_
