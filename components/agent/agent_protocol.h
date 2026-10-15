// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#ifndef UW_COMPONENTS_AGENT_AGENT_PROTOCOL_H_
#define UW_COMPONENTS_AGENT_AGENT_PROTOCOL_H_

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/values.h"

// The agent's side of an OpenAI-compatible chat-completions conversation:
// the messages uw sends and the tool calls a model answers with.
namespace uw {

struct ToolCall {
  std::string id;
  std::string name;
  // The arguments as the model wrote them: JSON text, maybe malformed.
  std::string arguments;
};

// One answer from the model: text for the user, tool calls to run, or both.
struct ModelTurn {
  std::string text;
  std::vector<ToolCall> calls;
};

// The tools of browser/agent/agent_tools.h, and recall, as a model is told
// about them.
base::ListValue AgentToolSchemas();
// The instructions every chat starts with.
base::DictValue AgentInstructions();
base::DictValue UserMessage(std::string_view text);
// The model's turn as it goes back into the conversation.
base::DictValue AssistantMessage(const ModelTurn& turn);
// A tool's answer to `call_id`.
base::DictValue ToolMessage(std::string_view call_id,
                            const base::DictValue& result);

// Empty `model` is refused.
std::optional<std::string> BuildAgentRequestBody(
    std::string_view model,
    const base::ListValue& messages,
    const base::ListValue& tools);

// The message in an error response such as {"error": {"message": "..."}},
// shortened to fit a line. Empty when there is none.
std::string ModelErrorMessage(std::string_view body);

// The first choice of a chat-completions response. Nothing for a response
// that is not one, or that has neither text nor a tool call.
std::optional<ModelTurn> ParseAgentResponse(std::string_view body);

}  // namespace uw

#endif  // UW_COMPONENTS_AGENT_AGENT_PROTOCOL_H_
