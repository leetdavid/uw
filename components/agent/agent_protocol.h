// Copyright 2026 The uw Authors. All rights reserved.

#ifndef UW_COMPONENTS_AGENT_AGENT_PROTOCOL_H_
#define UW_COMPONENTS_AGENT_AGENT_PROTOCOL_H_

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/values.h"
#include "build/build_config.h"

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
  // What the provider sent that uw does not read, as JSON, for the next
  // request to the same provider. See uw/components/ai_providers/formats.h.
  std::string kept;
};

// Whether the agent runs commands: run, screen, keys, and trigger. Terminal
// tabs run a shell on the Mac only so far; see docs/plans/windows.md.
inline constexpr bool kAgentHasTerminal = BUILDFLAG(IS_MAC);

// A tool as a model is told about it: its name, what it does, its
// parameters, and the names of those a call must give.
base::DictValue ToolSchema(std::string_view name,
                           std::string_view description,
                           base::DictValue properties,
                           base::ListValue required);
// One parameter of a tool.
base::DictValue ToolParameter(std::string_view type,
                              std::string_view description);

// The tools of browser/agent/agent_tools.h, and recall, as a model is told
// about them.
base::ListValue AgentToolSchemas();
// The instructions every chat starts with.
base::DictValue AgentInstructions();
base::DictValue UserMessage(std::string_view text);
// The model's turn as it goes back into the conversation.
base::DictValue AssistantMessage(const ModelTurn& turn);
// What a tool answers when it could not do what was asked.
base::DictValue ToolError(std::string_view message);
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
