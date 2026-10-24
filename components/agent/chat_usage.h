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
  // The price it was charged at, when the catalog knew one.
  std::optional<ModelPrice> price;

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

// What the chat's context and cost indicator and its card show. See
// docs/plans/context-and-cost.md.
struct UsageSummary {
  UsageSummary();
  UsageSummary(const UsageSummary&);
  UsageSummary& operator=(const UsageSummary&);
  ~UsageSummary();

  struct Spent {
    int64_t tokens = 0;
    double cost = 0;
  };
  struct Message {
    // The user's message, counting from 1.
    int message = 0;
    double cost = 0;
    int requests = 0;
  };

  // The agent's latest request: its input tokens, the limit it fills
  // toward, whether that limit is the model's own rather than where uw makes
  // room, and what filled it, in tokens.
  int64_t tokens = 0;
  int64_t limit = 0;
  bool model_limit = false;
  RequestParts parts;
  // Each of the agent's requests: its input tokens and its limit. The
  // requests uw made room before, counting from 1.
  std::vector<int64_t> per_request;
  std::vector<int64_t> limits;
  std::vector<int> made_room;

  // What the agent's requests spent, by kind of token.
  Spent fresh;
  Spent cache_read;
  Spent cache_write;
  Spent output;
  int agent_requests = 0;
  Spent checks;
  int check_count = 0;
  Spent runs;
  int run_count = 0;
  double total = 0;
  // Every request had a price, or a cost of its provider's.
  bool priced = true;
  // The kinds' costs add up to the total: no request's cost came without a
  // price to split it by.
  bool split = true;
  double cache_saved = 0;
  std::vector<Message> messages;
};
UsageSummary Summarize(const std::vector<UsageRecord>& records,
                       const std::vector<UsageTotal>& runs);

}  // namespace uw

#endif  // UW_COMPONENTS_AGENT_CHAT_USAGE_H_
