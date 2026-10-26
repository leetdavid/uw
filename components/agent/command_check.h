// Copyright 2026 The uw Authors. All rights reserved.

#ifndef UW_COMPONENTS_AGENT_COMMAND_CHECK_H_
#define UW_COMPONENTS_AGENT_COMMAND_CHECK_H_

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "uw/components/tab_organization/ai_protocol.h"

namespace uw {

// The command check: a quick model call that judges a terminal step of the
// agent before it runs, whether it is potentially dangerous and whether it
// may read a sensitive file. It sees the user's messages and the agent's
// tool calls, never what a page or a command showed. See ADR 0012 and
// docs/plans/agent-chats.md. No networking here.

// A step the agent wants to take in one of its terminal tabs: a command, or
// text and keys for the program running there.
struct TerminalStep {
  int tab = 0;
  std::string folder;
  std::string command;
  std::string text;
  std::vector<std::string> keys;
};

struct CommandCheckInput {
  // The user's messages and the agent's tool calls before the step, oldest
  // first, as AgentContext::UserAndActions() lists them.
  std::vector<std::string> chat;
  TerminalStep step;
  // Asks only whether the step may read a sensitive file, as Full auto does.
  bool file_only = false;
};

// The check's request to `model` in `format`. kChat is for a language model.
std::optional<std::string> BuildCommandCheckRequestBody(
    DecisionFormat format,
    std::string_view model,
    const CommandCheckInput& input);

struct CommandCheckAnswer {
  // The probability that the step is safe. Zero for `file_only`.
  double safe = 0.0;
  // The danger the check chose: "safe", "deletes", "sends", "system", or
  // "secrets".
  std::string danger;
  // What could go wrong, in a language model's own words.
  std::string reason;
  // The probability that the step reads a sensitive file, and the word of the
  // step that names it, if the check named one.
  double sensitive = 0.0;
  std::string file;
};

// Reads the answer to the request BuildCommandCheckRequestBody() made for
// `input`. Anything malformed is rejected, and counts as no answer.
std::optional<CommandCheckAnswer> ParseCommandCheckResponse(
    DecisionFormat format,
    std::string_view body,
    const CommandCheckInput& input);

// What uw does with the step.
struct CommandCheckVerdict {
  bool ask = false;
  bool sensitive = false;
  // The sensitive file, when the check named it.
  std::string file;
  // What the approval card says.
  std::string reason;
};

// Whether the user approves the step first. A missing answer asks. `model` is
// the chat's model, which sees what the step prints.
CommandCheckVerdict JudgeTerminalStep(
    const std::optional<CommandCheckAnswer>& answer,
    bool file_only,
    std::string_view model);

}  // namespace uw

#endif  // UW_COMPONENTS_AGENT_COMMAND_CHECK_H_
