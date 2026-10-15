// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#ifndef UW_COMPONENTS_AGENT_AGENT_CONTEXT_H_
#define UW_COMPONENTS_AGENT_AGENT_CONTEXT_H_

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/values.h"
#include "uw/components/agent/agent_protocol.h"

namespace uw {

// What the model sees of a chat. Every tool result is kept in full, but the
// model gets one short line for the results it no longer needs, and the
// recall tool brings any of them back. See docs/plans/agent-chats.md#context.
class AgentContext {
 public:
  AgentContext();
  AgentContext(const AgentContext&) = delete;
  AgentContext& operator=(const AgentContext&) = delete;
  ~AgentContext();

  // A context saved by ToValue(), or null when `saved` is not one.
  static std::unique_ptr<AgentContext> FromValue(const base::DictValue& saved);
  base::DictValue ToValue() const;

  void AddUser(std::string_view text);
  void AddAssistant(const ModelTurn& turn);
  // Records what a tool answered to the call `call_id` and returns its step
  // number. A reading of a tab shortens the older readings of that tab.
  int AddResult(std::string_view call_id,
                std::string_view tool,
                const base::DictValue& args,
                const base::DictValue& result);
  // What the recall tool answers to `args`.
  base::DictValue Recall(const base::DictValue& args) const;
  // Shortens every long result, for when the model has answered.
  void ShortenAll();
  // Lists `skills`, one per line, in the instructions every request starts
  // with. Empty for none.
  void SetSkills(std::string_view skills);
  // Answers each tool call of the model's last turn that has no result yet
  // with `error`. A model refuses a request with an unanswered call.
  void CloseOpenCalls(std::string_view error);

  // The request for `model`. Shortens the oldest long results until it fits
  // in `max_length` characters, or nothing is left to shorten.
  std::optional<std::string> Request(std::string_view model, size_t max_length);

 private:
  struct Result {
    int step = 0;
    std::string tool;
    base::DictValue args;
    base::DictValue result;
    // Where the result sits in `messages_`.
    size_t message = 0;
    bool shortened = false;
  };

  static bool CanShorten(const Result& result);
  void Shorten(Result& result);

  base::ListValue messages_;
  std::vector<Result> results_;
};

}  // namespace uw

#endif  // UW_COMPONENTS_AGENT_AGENT_CONTEXT_H_
