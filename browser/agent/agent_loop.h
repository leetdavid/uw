// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#ifndef UW_BROWSER_AGENT_AGENT_LOOP_H_
#define UW_BROWSER_AGENT_AGENT_LOOP_H_

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>

#include "base/functional/callback.h"
#include "base/memory/scoped_refptr.h"
#include "base/memory/weak_ptr.h"
#include "base/values.h"
#include "url/gurl.h"
#include "uw/browser/agent/agent_tools.h"
#include "uw/browser/tab_organization/remote_model_client.h"
#include "uw/components/agent/agent_context.h"
#include "uw/components/agent/agent_protocol.h"

namespace network {
class SharedURLLoaderFactory;
}

namespace uw {

// The model a chat talks to: its chat-completions address, its name, and its
// key, which may be empty.
struct AgentModel {
  GURL url;
  std::string model;
  std::string api_key;
};

// Runs one chat. Each message from the user goes to the model, and the model
// and the tools take turns until the model answers without calling a tool.
// Every step is reported as it ends. The model sees a short context; see
// docs/plans/agent-chats.md#context.
class AgentLoop {
 public:
  struct Step {
    enum class Kind { kTool, kAnswer, kFailed };
    Kind kind = Kind::kFailed;
    // For a tool: its name, what the model asked, and what the tool answered.
    std::string tool;
    base::DictValue args;
    base::DictValue result;
    // The model's answer, or what went wrong.
    std::string text;
  };
  using OnStep = base::RepeatingCallback<void(const Step&)>;

  AgentLoop(tabs::TabInterface& chat, AgentModel model, OnStep on_step);
  AgentLoop(const AgentLoop&) = delete;
  AgentLoop& operator=(const AgentLoop&) = delete;
  ~AgentLoop();

  // Sends the user's next message. Does nothing while the agent works.
  void Send(std::string_view text);
  bool working() const { return working_; }

 private:
  void AskModel();
  void OnModel(RemoteModelClient::Outcome outcome);
  void RunCall(ModelTurn turn, size_t index);
  void OnTool(ModelTurn turn,
              size_t index,
              base::DictValue args,
              base::DictValue result);
  void Finish(Step step);

  AgentTools tools_;
  const AgentModel model_;
  const OnStep on_step_;
  const scoped_refptr<network::SharedURLLoaderFactory> loader_;
  AgentContext context_;
  std::unique_ptr<RemoteModelClient> client_;
  int turns_ = 0;
  bool working_ = false;
  base::WeakPtrFactory<AgentLoop> weak_factory_{this};
};

}  // namespace uw

#endif  // UW_BROWSER_AGENT_AGENT_LOOP_H_
