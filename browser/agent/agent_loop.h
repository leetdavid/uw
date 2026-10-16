// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#ifndef UW_BROWSER_AGENT_AGENT_LOOP_H_
#define UW_BROWSER_AGENT_AGENT_LOOP_H_

#include <cstddef>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/files/file_path.h"
#include "base/functional/callback.h"
#include "base/functional/callback_helpers.h"
#include "base/memory/scoped_refptr.h"
#include "base/memory/weak_ptr.h"
#include "base/values.h"
#include "url/gurl.h"
#include "uw/browser/agent/agent_tools.h"
#include "uw/browser/ai_providers/models.h"
#include "uw/browser/ai_providers/remote_model_client.h"
#include "uw/components/agent/agent_context.h"
#include "uw/components/agent/agent_protocol.h"
#include "uw/components/agent/skills.h"
#include "uw/components/ai_providers/providers.h"

namespace network {
class SharedURLLoaderFactory;
}

namespace uw {

// How much a chat asks before it acts. Ask first asks before every click and
// typing, Auto before each change, and Full auto never. See
// docs/plans/agent-chats.md.
enum class ApprovalMode { kAskFirst, kAuto, kFullAuto };

// The model a chat talks to, and its key, which may be empty.
struct AgentModel {
  AiConnectionConfig connection;
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
    // For a tool: its step number, its name, what the model asked, and what
    // the tool answered.
    int number = 0;
    std::string tool;
    base::DictValue args;
    base::DictValue result;
    // The model's answer, or what went wrong.
    std::string text;
  };
  using OnStep = base::RepeatingCallback<void(const Step&)>;

  // Goes on from `context` when there is one. `on_change` runs when the
  // model's turn arrives, before its tools run, so the turn can be saved.
  AgentLoop(tabs::TabInterface& chat,
            AgentModel model,
            OnStep on_step,
            std::unique_ptr<AgentContext> context = nullptr,
            base::RepeatingClosure on_change = base::DoNothing());
  AgentLoop(const AgentLoop&) = delete;
  AgentLoop& operator=(const AgentLoop&) = delete;
  ~AgentLoop();

  // Sends the user's next message. Does nothing while the agent works.
  void Send(std::string_view text);
  // Picks the chat up again after uw quit while the agent was working.
  void Resume();
  // Asks the model again after its request failed, with the chat as it is.
  void TryAgain();
  bool working() const { return working_; }
  // Talks to `model` from the next request on.
  void SetModel(AgentModel model) { model_ = std::move(model); }
  // Asks for approval as `mode` says from the next step on. Auto until set.
  void SetMode(ApprovalMode mode) { mode_ = mode; }
  // The skills the agent may load from now on.
  void SetSkills(const std::vector<Skill>& skills);
  // Asks the user whether to save the skill a save_skill call describes,
  // saves it if so, and answers with what the model is told.
  using SaveSkill =
      base::RepeatingCallback<void(base::DictValue args,
                                   base::OnceCallback<void(base::DictValue)>)>;
  void SetSkillSaver(SaveSkill save_skill) {
    save_skill_ = std::move(save_skill);
  }
  // The user's answer to a step that needs approval. A decline may carry a
  // note for the model.
  struct ApprovalAnswer {
    enum class Choice { kApprove, kDecline, kStop };
    Choice choice = Choice::kDecline;
    std::string note;
  };
  // Asks the user to approve a step and answers. It also answers the engine
  // when it asks about a sensitive site. Without one, every step runs, and
  // the engine refuses sensitive sites.
  using Approve =
      base::RepeatingCallback<void(AgentTools::Approval,
                                   base::OnceCallback<void(ApprovalAnswer)>)>;
  void SetApprover(Approve approve);
  const AgentContext& context() const { return *context_; }
  AgentTools& tools() { return tools_; }

 private:
  void AskModel();
  void OnModel(RemoteModelClient::Outcome outcome);
  void RunCall(ModelTurn turn, size_t index);
  void LoadSkill(ModelTurn turn, size_t index, base::DictValue args);
  void CallTool(ModelTurn turn, size_t index, base::DictValue args);
  void OnApproval(ModelTurn turn,
                  size_t index,
                  base::DictValue args,
                  ApprovalAnswer answer);
  void ConfirmSite(AgentTools::Approval approval,
                   base::OnceCallback<void(bool)> done);
  void OnSiteAnswer(base::OnceCallback<void(bool)> done, ApprovalAnswer answer);
  void OnToolCalled(ModelTurn turn,
                    size_t index,
                    base::DictValue args,
                    base::DictValue result);
  void OnTool(ModelTurn turn,
              size_t index,
              base::DictValue args,
              base::DictValue result);
  void Finish(Step step);

  AgentTools tools_;
  AgentModel model_;
  const OnStep on_step_;
  const scoped_refptr<network::SharedURLLoaderFactory> loader_;
  const std::unique_ptr<AgentContext> context_;
  const base::RepeatingClosure on_change_;
  // Each skill's SKILL.md by the skill's name.
  std::map<std::string, base::FilePath> skills_;
  SaveSkill save_skill_;
  Approve approve_;
  ApprovalMode mode_ = ApprovalMode::kAuto;
  // The user's no to the engine's question about a site, during the step
  // under way.
  std::optional<ApprovalAnswer> site_refused_;
  std::unique_ptr<RemoteModelClient> client_;
  int turns_ = 0;
  bool working_ = false;
  base::WeakPtrFactory<AgentLoop> weak_factory_{this};
};

}  // namespace uw

#endif  // UW_BROWSER_AGENT_AGENT_LOOP_H_
