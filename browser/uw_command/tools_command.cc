// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include <algorithm>
#include <array>
#include <deque>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "base/callback_list.h"
#include "base/functional/bind.h"
#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/no_destructor.h"
#include "base/strings/strcat.h"
#include "base/token.h"
#include "base/values.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "components/prefs/pref_service.h"
#include "components/tabs/public/tab_interface.h"
#include "uw/browser/agent/agent_loop.h"
#include "uw/browser/agent/agent_tools.h"
#include "uw/browser/agent/chat_notification.h"
#include "uw/browser/agent/chat_tab.h"
#include "uw/browser/agent/run_window.h"
#include "uw/browser/agent/run_window_mark.h"
#include "uw/browser/tab_organization/ai_connection.h"
#include "uw/browser/ui/webui/uw_chat/schedule_card.h"
#include "uw/browser/uw_command/command_channel.h"
#include "uw/browser/uw_command/command_handler.h"
#include "uw/browser/uw_command/command_workspace.h"
#include "uw/browser/workspaces/workspace_list.h"
#include "uw/components/agent/agent_protocol.h"
#include "uw/components/uw_command/command_messages.h"

namespace uw {

namespace {

// The agent's tools an outside agent may use. See
// docs/plans/uw-command.md#the-tool-server.
constexpr auto kPageTools = std::to_array<std::string_view>(
    {"open", "navigate", "read", "click", "type"});

// The page tools as MCP describes a tool: a name, a description, and the
// JSON Schema of its arguments.
base::ListValue PageTools() {
  base::ListValue tools;
  for (const base::Value& schema : AgentToolSchemas()) {
    const base::DictValue* function = schema.GetDict().FindDict("function");
    const std::string* name = function ? function->FindString("name") : nullptr;
    if (!name || !std::ranges::contains(kPageTools, *name)) {
      continue;
    }
    tools.Append(
        base::DictValue()
            .Set("name", *name)
            .Set("description", *function->FindString("description"))
            .Set("inputSchema", function->FindDict("parameters")->Clone()));
  }
  return tools;
}

base::DictValue Error(std::string_view text) {
  return base::DictValue().Set("error", text);
}

// A page step's approval as the command reads a chat's.
base::DictValue Described(const AgentTools::Approval& approval) {
  return base::DictValue()
      .Set("tab", approval.tab)
      .Set("site", approval.site)
      .Set("action", approval.action)
      .Set("reason", approval.reason)
      .Set("folder", "")
      .Set("command", "")
      .Set("text", "")
      .Set("keys", base::ListValue())
      .Set("connector", "")
      .Set("tool", "")
      .Set("arguments", base::DictValue())
      .Set("canAlwaysAllow", false)
      .Set("sensitive", false)
      .Set("file", "");
}

ApprovalMode ModeFrom(const std::string* name, const PrefService& prefs) {
  if (!name) {
    return ModeNamed(prefs.GetString(kAgentApprovalModePref));
  }
  if (*name == "ask-first") {
    return ApprovalMode::kAskFirst;
  }
  return *name == "full-auto" ? ApprovalMode::kFullAuto : ApprovalMode::kAuto;
}

class ToolSession;

std::vector<std::unique_ptr<ToolSession>>& Sessions() {
  static base::NoDestructor<std::vector<std::unique_ptr<ToolSession>>> all;
  return *all;
}

// Lets go of `session`, which deletes it.
void Forget(ToolSession* session);

// An outside agent's tools for as long as its command is connected. Its tabs
// open out of sight, under a tab of its own in the run window, and close
// when the command goes. Steps run one at a time; a step that needs approval
// asks through the command, which asks the client, or with a client that
// can't ask, through uw's notification.
class ToolSession {
 public:
  ToolSession(Profile* profile,
              std::string client,
              ApprovalMode mode,
              base::WeakPtr<CommandConnection> connection)
      : profile_(profile),
        client_(std::move(client)),
        mode_(mode),
        connection_(std::move(connection)) {
    closed_ =
        connection_->OnClose(base::BindOnce(&Forget, base::Unretained(this)));
  }

  ~ToolSession() {
    CloseStepNotification(profile_, notification_);
    tools_.reset();
    if (tabs::TabInterface* run_tab = run_tab_.Get()) {
      CloseRunTabs(*run_tab);
    }
  }

  CommandConnection* connection() { return connection_.get(); }

  // Runs `tool` once the steps before it are done. With `asks`, a step that
  // needs approval answers the command at once with what waits, and Answer
  // carries it on.
  void Call(base::Value id,
            std::string tool,
            base::DictValue args,
            bool asks,
            CommandReply reply) {
    // A client that never answered what waits has let it go.
    if (step_ && !step_->held.empty() && !step_->reply) {
      Answered(false, std::string());
    }
    queue_.push_back(Step{std::move(id), std::move(tool), std::move(args), asks,
                          std::move(reply)});
    Next();
  }

  // The user's answer, through the command, to what step `held` waits on.
  void Answer(base::Value id,
              const std::string& held,
              bool approved,
              std::string note,
              CommandReply reply) {
    if (!step_ || step_->held.empty() || step_->held != held) {
      std::move(reply).Run(
          CommandError(id, kCommandUsage, "Nothing waits for that answer."));
      return;
    }
    step_->id = std::move(id);
    step_->reply = std::move(reply);
    Answered(approved, std::move(note));
  }

 private:
  struct Step {
    base::Value id;
    std::string tool;
    base::DictValue args;
    bool asks = false;
    // The request that hears how the step ends, or what it waits on.
    CommandReply reply;
    // What the command's client is asked, until it answers.
    std::string held;
    base::OnceCallback<void(bool approved, std::string note)> answered;
    bool site_refused = false;
  };

  void Next() {
    if (step_ || queue_.empty()) {
      return;
    }
    step_ = std::move(queue_.front());
    queue_.pop_front();
    if (!tools_ && !Start()) {
      Done(Error("uw couldn't open a tab for the agent."));
      return;
    }
    if (mode_ != ApprovalMode::kFullAuto) {
      if (std::optional<AgentTools::Approval> approval = tools_->ApprovalFor(
              step_->tool, step_->args,
              /*every_step=*/mode_ == ApprovalMode::kAskFirst)) {
        Ask(std::move(*approval), base::BindOnce(&ToolSession::OnStepAnswer,
                                                 weak_factory_.GetWeakPtr()));
        return;
      }
    }
    Run();
  }

  // Opens the session's own tab in the run window, which the agent's tabs
  // open under.
  bool Start() {
    tabs::TabInterface* run_tab = OpenRunTab(profile_);
    if (!run_tab) {
      return false;
    }
    run_tab_ = run_tab->GetHandle();
    tools_ = std::make_unique<AgentTools>(*run_tab);
    tools_->SetConfirmSite(base::BindRepeating(&ToolSession::ConfirmSite,
                                               weak_factory_.GetWeakPtr()));
    return true;
  }

  void Run() {
    tools_->Call(
        step_->tool, step_->args,
        base::BindOnce(&ToolSession::Done, weak_factory_.GetWeakPtr()));
  }

  void OnStepAnswer(bool approved, std::string note) {
    if (approved) {
      Run();
      return;
    }
    base::DictValue result =
        Error("The user did not approve this step, so it did not happen.");
    if (!note.empty()) {
      result.Set("user_said", note);
    }
    Done(std::move(result));
  }

  // The engine asks, in the middle of a step, to act on a site that Chromium
  // marks as sensitive.
  void ConfirmSite(AgentTools::Approval approval,
                   base::OnceCallback<void(bool)> done) {
    if (mode_ == ApprovalMode::kFullAuto || !step_) {
      std::move(done).Run(mode_ == ApprovalMode::kFullAuto);
      return;
    }
    Ask(std::move(approval), base::BindOnce(
                                 [](base::WeakPtr<ToolSession> self,
                                    base::OnceCallback<void(bool)> done,
                                    bool approved, std::string note) {
                                   if (self && self->step_ && !approved) {
                                     self->step_->site_refused = true;
                                   }
                                   std::move(done).Run(approved);
                                 },
                                 weak_factory_.GetWeakPtr(), std::move(done)));
  }

  // Brings the tab the step waits on into the front window, unselected, and
  // asks.
  void Ask(AgentTools::Approval approval,
           base::OnceCallback<void(bool, std::string)> answered) {
    step_->answered = std::move(answered);
    BringTab(approval.tab);
    if (step_->asks) {
      step_->held = base::Token::CreateRandom().ToString();
      Reply(base::DictValue().Set("waiting",
                                  base::DictValue()
                                      .Set("step", step_->held)
                                      .Set("approval", Described(approval))));
      return;
    }
    asked_ = std::move(approval);
    Notify();
  }

  // Asks in uw's notification, for a client that can't ask.
  void Notify() {
    notification_ =
        base::StrCat({"uw-step-", base::Token::CreateRandom().ToString()});
    const std::string step =
        asked_.site.empty()
            ? asked_.action
            : base::StrCat({asked_.action, " on ", asked_.site});
    AskAboutStep(profile_, notification_,
                 base::StrCat({client_, " waits for your approval"}), step,
                 base::BindRepeating(&ToolSession::OnNotificationAnswer,
                                     weak_factory_.GetWeakPtr()));
  }

  void OnNotificationAnswer(std::optional<bool> approved) {
    notification_.clear();
    if (approved) {
      Answered(*approved, std::string());
      return;
    }
    // A click on the notification itself shows the tab, and asks again.
    std::map<int, tabs::TabInterface*> open = tools_->OpenTabs();
    if (auto tab = open.find(asked_.tab); tab != open.end()) {
      BringTab(asked_.tab);
      GoToTab(tab->second);
    }
    Notify();
  }

  void Answered(bool approved, std::string note) {
    step_->held.clear();
    if (step_->answered) {
      std::move(step_->answered).Run(approved, std::move(note));
    }
  }

  void BringTab(int number) {
    std::map<int, tabs::TabInterface*> open = tools_->OpenTabs();
    auto tab = open.find(number);
    if (tab == open.end() ||
        !IsRunWindow(tab->second->GetBrowserWindowInterface())) {
      return;
    }
    if (BrowserWindowInterface* front = FrontWindow(profile_)) {
      MoveRunTabs(*tab->second, *front);
    }
  }

  void Done(base::DictValue result) {
    if (step_->site_refused) {
      result = Error(
          "The user did not approve acting on this site, so the step did not "
          "happen.");
    }
    Reply(base::DictValue().Set("result", std::move(result)));
    step_.reset();
    Next();
  }

  void Reply(base::DictValue value) {
    if (step_->reply) {
      std::move(step_->reply)
          .Run(CommandResult(step_->id, base::Value(std::move(value))));
    }
  }

  const raw_ptr<Profile> profile_;
  const std::string client_;
  const ApprovalMode mode_;
  base::WeakPtr<CommandConnection> connection_;
  base::CallbackListSubscription closed_;
  tabs::TabHandle run_tab_;
  std::unique_ptr<AgentTools> tools_;
  std::deque<Step> queue_;
  std::optional<Step> step_;
  // What uw's notification asks, and its id while it shows.
  AgentTools::Approval asked_;
  std::string notification_;
  base::WeakPtrFactory<ToolSession> weak_factory_{this};
};

void Forget(ToolSession* session) {
  std::erase_if(Sessions(),
                [&](const auto& owned) { return owned.get() == session; });
}

ToolSession* SessionOf(const CommandConnection* connection) {
  for (const std::unique_ptr<ToolSession>& session : Sessions()) {
    if (session->connection() == connection) {
      return session.get();
    }
  }
  return nullptr;
}

// tools.start: the session for this connection, in the workspace it names.
void StartSession(CommandRequest request,
                  CommandReply reply,
                  base::WeakPtr<CommandConnection> connection) {
  base::expected<CommandWorkspace, std::string> chosen =
      ChooseWorkspace(request.params);
  if (!chosen.has_value()) {
    std::move(reply).Run(
        CommandError(request.id, kCommandUsage, chosen.error()));
    return;
  }
  LoadWorkspace(
      std::move(*chosen),
      base::BindOnce(
          [](CommandRequest request, CommandReply reply,
             base::WeakPtr<CommandConnection> connection, Profile* profile,
             const std::string& id) {
            if (!profile || !connection) {
              std::move(reply).Run(
                  CommandError(request.id, kCommandFailed,
                               "uw couldn't open that workspace."));
              return;
            }
            if (ToolSession* old = SessionOf(connection.get())) {
              Forget(old);
            }
            const std::string* client = request.params.FindString("client");
            Sessions().push_back(std::make_unique<ToolSession>(
                profile, client ? *client : std::string("An outside agent"),
                ModeFrom(request.params.FindString("mode"),
                         *profile->GetPrefs()),
                connection));
            const WorkspaceList::Workspace* workspace =
                WorkspaceList::Get().Find(id);
            std::move(reply).Run(CommandResult(
                request.id,
                base::Value(base::DictValue().Set(
                    "workspace", workspace ? workspace->name : ""))));
          },
          std::move(request), std::move(reply), std::move(connection)));
}

}  // namespace

void RunToolsCommand(CommandRequest request,
                     CommandReply reply,
                     base::WeakPtr<CommandConnection> connection) {
  if (request.method == "tools.list") {
    std::move(reply).Run(CommandResult(
        request.id, base::Value(base::DictValue().Set("tools", PageTools()))));
    return;
  }
  if (request.method == "tools.start") {
    StartSession(std::move(request), std::move(reply), std::move(connection));
    return;
  }
  ToolSession* session = connection ? SessionOf(connection.get()) : nullptr;
  if (!session) {
    std::move(reply).Run(
        CommandError(request.id, kCommandUsage, "Start with tools.start."));
    return;
  }
  if (request.method == "tools.call") {
    const std::string* tool = request.params.FindString("tool");
    if (!tool || !std::ranges::contains(kPageTools, *tool)) {
      std::move(reply).Run(
          CommandError(request.id, kCommandUsage,
                       base::StrCat({"uw mcp serve has no tool called ",
                                     tool ? *tool : std::string(), "."})));
      return;
    }
    const base::DictValue* args = request.params.FindDict("args");
    session->Call(
        request.id.Clone(), *tool, args ? args->Clone() : base::DictValue(),
        request.params.FindBool("asks").value_or(false), std::move(reply));
    return;
  }
  if (request.method == "tools.answer") {
    const std::string* step = request.params.FindString("step");
    const std::string* choice = request.params.FindString("choice");
    const std::string* note = request.params.FindString("note");
    session->Answer(request.id.Clone(), step ? *step : std::string(),
                    choice && *choice == "allow", note ? *note : std::string(),
                    std::move(reply));
    return;
  }
  std::move(reply).Run(
      CommandError(request.id, kMethodNotFound,
                   base::StrCat({"This uw can't answer ", request.method,
                                 ". Update uw, then try again."})));
}

}  // namespace uw
