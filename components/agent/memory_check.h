// Copyright 2026 The uw Authors. All rights reserved.

#ifndef UW_COMPONENTS_AGENT_MEMORY_CHECK_H_
#define UW_COMPONENTS_AGENT_MEMORY_CHECK_H_

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "uw/components/tab_organization/ai_protocol.h"

namespace uw {

// The memory check: a quick model call that judges a sentence the agent wants
// to keep in agent memory, before it is kept: whether the user said or
// clearly meant it, and whether it holds a secret or a sensitive topic. It
// sees the user's own messages and the sentence, never page text, attached
// material, or tool results. See docs/plans/agent-memory.md. No networking
// here.

struct MemoryCheckInput {
  // The user's own messages in the chat, oldest first.
  std::vector<std::string> user;
  // The sentence the agent wants to keep.
  std::string memory;
};

// The check's request to `model` in `format`. kChat is for a language model.
std::optional<std::string> BuildMemoryCheckRequestBody(
    DecisionFormat format,
    std::string_view model,
    const MemoryCheckInput& input);

struct MemoryCheckAnswer {
  // The probability that the user said or clearly meant the sentence.
  double from_user = 0.0;
  // The probability that it holds neither a secret nor a sensitive topic
  // the user did not ask to remember.
  double ordinary = 0.0;
  // What the check found it holds: "ordinary", "secret", or "sensitive".
  std::string holds;
};

// Reads the answer to the request BuildMemoryCheckRequestBody() made.
// Anything malformed is rejected, and counts as no answer.
std::optional<MemoryCheckAnswer> ParseMemoryCheckResponse(
    DecisionFormat format,
    std::string_view body);

struct MemoryCheckVerdict {
  bool keep = false;
  // Why it was not kept, as the agent is told.
  std::string reason;
};

// Whether the sentence is kept. A missing or unsure answer keeps nothing.
MemoryCheckVerdict JudgeMemory(const std::optional<MemoryCheckAnswer>& answer);

}  // namespace uw

#endif  // UW_COMPONENTS_AGENT_MEMORY_CHECK_H_
