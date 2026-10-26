// Copyright 2026 The uw Authors. All rights reserved.

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
#include "base/memory/raw_ptr.h"
#include "base/memory/scoped_refptr.h"
#include "base/memory/weak_ptr.h"
#include "base/types/expected.h"
#include "base/values.h"
#include "url/gurl.h"
#include "uw/browser/agent/agent_tools.h"
#include "uw/browser/agent/memory_store.h"
#include "uw/browser/agent/quick_check.h"
#include "uw/browser/ai_providers/models.h"
#include "uw/browser/ai_providers/remote_model_client.h"
#include "uw/components/agent/agent_context.h"
#include "uw/components/agent/agent_protocol.h"
#include "uw/components/agent/chat_usage.h"
#include "uw/components/agent/command_check.h"
#include "uw/components/agent/memory_tools.h"
#include "uw/components/agent/skills.h"
#include "uw/components/ai_providers/providers.h"
#include "uw/components/ai_providers/usage.h"
#include "uw/components/connectors/connector_tools.h"

class Profile;

namespace network {
class SharedURLLoaderFactory;
}

namespace uw {

// How much a chat asks before it acts. Ask first asks before every click,
// typing, command, and set of keys. Auto asks before each change on a site and
// each command the command check finds potentially dangerous. Full auto never
// asks, except before a command that may read a sensitive file, which every
// mode asks about. See docs/plans/agent-chats.md.
enum class ApprovalMode { kAskFirst, kAuto, kFullAuto };

// What a chat does with its profile's agent memory: nothing, as a private
// chat or a chat kept out of memory; recall only, as a scheduled run; or
// recall and keep. See docs/plans/agent-memory.md.
enum class MemoryUse { kOff, kRecall, kRecallAndKeep };

// The model a chat talks to, and its key, which may be empty.
struct AgentModel {
  AiConnectionConfig connection;
  std::string api_key;
  // From the models.dev catalog: its price, and how many input tokens a
  // request may hold, 0 when unknown.
  std::optional<ModelPrice> price;
  int64_t input_limit = 0;
};

// The model `choice` names, with its key, price, and input limit. Nothing
// while the choice is incomplete.
std::optional<AgentModel> AgentModelFor(Profile& profile,
                                        const ModelChoice& choice);

// Runs one chat. Each message from the user goes to the model, and the model
// and the tools take turns until the model answers without calling a tool.
// Every step is reported as it ends. The model sees a short context; see
// docs/plans/agent-chats.md#context.
class AgentLoop {
 public:
  struct Step {
    enum class Kind { kTool, kAnswer, kFailed, kStoppedByUser };
    Kind kind = Kind::kFailed;
    // For a tool: its step number, its name, what the model asked, and what
    // the tool answered.
    int number = 0;
    std::string tool;
    base::DictValue args;
    base::DictValue result;
    // The model's answer, or what went wrong.
    std::string text;
    // A failure to reach the model: no model is set, or its request failed,
    // as when the provider is down, rejects the key, or the Mac is offline.
    bool model_unreachable = false;
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
  // `material` is text piped into the uw command with it; see
  // AgentContext::AddMaterial.
  void Send(std::string_view text,
            std::vector<std::string> images = {},
            std::string_view material = {});
  // Picks the chat up again after uw quit while the agent was working.
  void Resume();
  // Asks the model again after its request failed, with the chat as it is.
  void TryAgain();
  // Ends the run: a waiting model request is dropped, and a step under way
  // finishes first.
  void Stop();
  // Puts the user's `text` in after the step under way, before the next model
  // request. A waiting model request is sent again with it. Sends it when
  // the agent is not working.
  void Steer(std::string_view text, std::vector<std::string> images = {});
  bool working() const { return working_; }
  // Talks to `model` from the next request on.
  void SetModel(AgentModel model) { model_ = std::move(model); }
  // Asks for approval as `mode` says from the next step on. Auto until set.
  void SetMode(ApprovalMode mode) { mode_ = mode; }
  // What the chat does with agent memory from the next message on. Off until
  // set. While the profile's memory is paused, it does nothing.
  void SetMemoryUse(MemoryUse use) { memory_use_ = use; }
  // The chat's id, which each memory it keeps names as where it came from.
  void SetChatId(std::string id) { chat_id_ = std::move(id); }
  // The skills the agent may load from now on: those of `skills` that are
  // not only for the user.
  void SetSkills(const std::vector<Skill>& skills);
  // The user started `skill` with its name, so for the rest of the chat the
  // agent may read its files, even if it is only for the user.
  void StartedSkill(const Skill& skill);
  // The connectors this chat may use, with the tools the user left on. The
  // model loads one with the connector tool. See docs/plans/connectors.md.
  void SetConnectors(std::vector<ConnectorTools> connectors);
  // What the chat's scheduled runs reported, as AgentContext takes it.
  void SetScheduledRuns(std::string_view runs) {
    context_->SetScheduledRuns(runs);
  }
  // Whether the model sees images. A connector's images are left out for one
  // that does not.
  void SetSeesImages(bool sees) { sees_images_ = sees; }
  // Turns a connector off or on for this chat, which keeps it.
  void SetConnectorOn(std::string_view name, bool on) {
    context_->SetConnectorOn(name, on);
  }
  // Asks the user whether to save the skill a save_skill call describes,
  // saves it if so, and answers with what the model is told.
  using SaveSkill =
      base::RepeatingCallback<void(base::DictValue args,
                                   base::OnceCallback<void(base::DictValue)>)>;
  void SetSkillSaver(SaveSkill save_skill) {
    save_skill_ = std::move(save_skill);
  }

  // A run of a scheduled chat ends with the report tool. Once this is set,
  // the model is offered it, and `report` answers each call.
  using Report =
      base::RepeatingCallback<base::DictValue(const base::DictValue& args)>;
  void SetReport(Report report);
  // A chat's agent fills in the schedule card with the schedule tool. Once
  // this is set, the model is offered it, and `fill` opens the card for each
  // call and answers it.
  using FillScheduleCard =
      base::RepeatingCallback<base::DictValue(const base::DictValue& args)>;
  void SetScheduleCard(FillScheduleCard fill);
  // A chat's agent can leave a command running as a trigger. Once these are
  // set, the model is offered the trigger tools, and a trigger call runs
  // through the same approvals as a command.
  void SetTriggers(AgentTools::StartTrigger start,
                   AgentTools::StopTrigger stop);
  // The user's answer to a step that needs approval. A decline may carry a
  // note for the model.
  struct ApprovalAnswer {
    // A connector's tool can also be allowed for the rest of the chat, or
    // always.
    enum class Choice {
      kApprove,
      kDecline,
      kStop,
      kAllowForChat,
      kAlwaysAllow,
    };
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
  // Puts the memory the chat may use in the request, then asks the model.
  void RecallThenAsk();
  void OnMemoryLoaded();
  // What the chat may do with memory now: nothing while the profile's memory
  // is paused.
  MemoryUse MemoryUseNow() const;
  void AskModel();
  void OnModel(RemoteModelClient::Outcome outcome);
  // The most characters a request may reach: kMaxRequestLength, or less for
  // a model whose input limit is smaller.
  size_t MaxRequestLength() const;
  void RunCall(ModelTurn turn, size_t index);
  void LoadSkill(ModelTurn turn, size_t index, base::DictValue args);
  void UpdateConnectorTools();
  void LoadConnector(ModelTurn turn, size_t index, base::DictValue args);
  // Whether the user must approve a call to the connector tool `name`.
  bool ConnectorNeedsApproval(const std::string& name) const;
  void CallConnector(ModelTurn turn, size_t index, base::DictValue args);
  void OnConnectorAnswer(ModelTurn turn,
                         size_t index,
                         base::DictValue args,
                         base::expected<mcp::Content, std::string> answer);
  void CallTool(ModelTurn turn, size_t index, base::DictValue args);
  void RunMemoryTool(ModelTurn turn, size_t index, base::DictValue args);
  void OnMemoryCheck(std::optional<QuickCheck::Answer> answer);
  void OnKept(ModelTurn turn,
              size_t index,
              base::DictValue args,
              base::expected<MemoryStore::KeepResult, std::string> kept);
  void OnForgot(ModelTurn turn,
                size_t index,
                base::DictValue args,
                std::optional<Memory> forgot);
  void OnFound(ModelTurn turn,
               size_t index,
               base::DictValue args,
               std::vector<Memory> found);
  // Counts what a command or memory check used toward the chat's cost.
  void RecordCheckUsage(const QuickCheck* check, UsageRecord::Kind kind);
  // Asks the command check about a terminal step that `approval` describes,
  // and runs it or asks the user, as its answer says.
  void CheckStep(ModelTurn turn,
                 size_t index,
                 base::DictValue args,
                 AgentTools::Approval approval);
  void OnCheck(std::optional<QuickCheck::Answer> answer);
  // Drops a command or memory check under way, after Stop or a steer, and
  // leaves its step to RunCall.
  void EndCheck();
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
  const raw_ptr<Profile> profile_;
  AgentModel model_;
  const OnStep on_step_;
  const scoped_refptr<network::SharedURLLoaderFactory> loader_;
  const std::unique_ptr<AgentContext> context_;
  const base::RepeatingClosure on_change_;
  // Each skill's SKILL.md by the skill's name: those the agent may pick, and
  // those the user started.
  std::map<std::string, base::FilePath> skills_;
  std::map<std::string, base::FilePath> started_skills_;
  std::vector<ConnectorTools> connectors_;
  // Each loaded connector's tools by the name the model calls them.
  struct ConnectorTool {
    std::string connector;
    mcp::Tool tool;
  };
  std::map<std::string, ConnectorTool> connector_tools_;
  bool sees_images_ = true;
  // Images connectors' tools answered with in the current answer, shown to
  // the model before it is asked again.
  std::vector<std::pair<std::string, std::vector<std::string>>> tool_images_;
  SaveSkill save_skill_;
  Report report_;
  FillScheduleCard schedule_card_;
  Approve approve_;
  ApprovalMode mode_ = ApprovalMode::kAuto;
  MemoryUse memory_use_ = MemoryUse::kOff;
  // What the latest request offered the model of memory.
  MemoryUse offered_memory_ = MemoryUse::kOff;
  std::string chat_id_;
  // The user's no to the engine's question about a site, during the step
  // under way.
  std::optional<ApprovalAnswer> site_refused_;
  std::unique_ptr<RemoteModelClient> client_;
  // The command check under way, and the step it is about.
  struct Check {
    ModelTurn turn;
    size_t index = 0;
    base::DictValue args;
    AgentTools::Approval approval;
    CommandCheckInput input;
    bool file_only = false;
    std::unique_ptr<QuickCheck> client;
  };
  std::unique_ptr<Check> check_;
  // The memory check under way, and the remember call it is about.
  struct MemoryCheck {
    ModelTurn turn;
    size_t index = 0;
    base::DictValue args;
    std::unique_ptr<QuickCheck> client;
  };
  std::unique_ptr<MemoryCheck> memory_check_;
  int turns_ = 0;
  bool working_ = false;
  // A model request is under way.
  bool asking_ = false;
  // Stop or Steer came while a step was under way.
  bool stopping_ = false;
  struct Message {
    std::string text;
    // data: URLs.
    std::vector<std::string> images;
  };
  std::optional<Message> steer_;
  // Images the model recalled in its current answer, shown to it before it
  // is asked again.
  std::vector<int> recalled_images_;
  base::WeakPtrFactory<AgentLoop> weak_factory_{this};
};

}  // namespace uw

#endif  // UW_BROWSER_AGENT_AGENT_LOOP_H_
