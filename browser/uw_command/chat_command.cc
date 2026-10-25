// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/json/json_reader.h"
#include "base/no_destructor.h"
#include "base/strings/strcat.h"
#include "base/task/sequenced_task_runner.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "base/values.h"
#include "chrome/browser/profiles/profile.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/referrer.h"
#include "ui/base/page_transition_types.h"
#include "uw/browser/agent/chat_file.h"
#include "uw/browser/agent/chat_index.h"
#include "uw/browser/agent/chat_tab.h"
#include "uw/browser/agent/run_window.h"
#include "uw/browser/ui/webui/uw_chat/uw_chat_handler.h"
#include "uw/browser/uw_command/command_channel.h"
#include "uw/browser/uw_command/command_handler.h"
#include "uw/browser/uw_command/command_workspace.h"
#include "uw/components/uw_command/command_messages.h"

namespace uw {

namespace {

// How long a chat's page may take to open in the run window.
constexpr base::TimeDelta kOpenWait = base::Seconds(30);

std::string_view KindName(mojom::StepKind kind) {
  switch (kind) {
    case mojom::StepKind::kTool:
      return "tool";
    case mojom::StepKind::kAnswer:
      return "answer";
    case mojom::StepKind::kFailed:
      return "failed";
    case mojom::StepKind::kUser:
      return "user";
    case mojom::StepKind::kStopped:
      return "stopped";
    case mojom::StepKind::kResumed:
      return "resumed";
    case mojom::StepKind::kModel:
      return "model";
    case mojom::StepKind::kStoppedByUser:
      return "stopped-by-user";
    case mojom::StepKind::kRun:
      return "run";
    case mojom::StepKind::kRunSkipped:
      return "run-skipped";
    case mojom::StepKind::kWake:
      return "wake";
    case mojom::StepKind::kTriggerStopped:
      return "trigger-stopped";
  }
}

base::Value Parsed(const std::string& json) {
  std::optional<base::Value> parsed =
      base::JSONReader::Read(json, base::JSON_PARSE_RFC);
  return parsed ? std::move(*parsed) : base::Value(base::DictValue());
}

base::Value Described(const mojom::Approval& approval) {
  base::DictValue described;
  described.Set("tab", approval.tab);
  described.Set("site", approval.site);
  described.Set("action", approval.action);
  described.Set("reason", approval.reason);
  described.Set("folder", approval.folder);
  described.Set("command", approval.command);
  described.Set("text", approval.text);
  base::ListValue keys;
  for (const std::string& key : approval.keys) {
    keys.Append(key);
  }
  described.Set("keys", std::move(keys));
  described.Set("connector", approval.connector);
  described.Set("tool", approval.tool);
  described.Set("arguments", Parsed(approval.arguments));
  described.Set("canAlwaysAllow", approval.can_always_allow);
  described.Set("sensitive", approval.sensitive);
  described.Set("file", approval.file);
  return base::Value(std::move(described));
}

class ChatSession;

std::vector<std::unique_ptr<ChatSession>>& Sessions() {
  static base::NoDestructor<std::vector<std::unique_ptr<ChatSession>>> all;
  return *all;
}

// Lets go of `session`, which deletes it.
void Forget(ChatSession* session);

// A chat the command started or picked up again. Its steps and approvals go
// to the command while it is attached. Ctrl-C, or the command going away,
// stops the chat, unless the command let go of it first, as when it leaves
// a step waiting for you. See docs/plans/uw-command.md#chats.
class ChatSession : public UwChatHandler::Watcher {
 public:
  ChatSession(UwChatHandler* handler,
              base::WeakPtr<CommandConnection> connection)
      : handler_(handler), connection_(std::move(connection)) {
    handler_->AddWatcher(this);
    closed_ = connection_->OnClose(base::BindOnce(
        &ChatSession::OnCommandGone, weak_factory_.GetWeakPtr()));
  }

  ~ChatSession() override {
    if (handler_) {
      handler_->RemoveWatcher(this);
    }
  }

  const std::string& chat() const { return chat_; }
  CommandConnection* connection() { return connection_.get(); }

  // The command let go: the chat goes on without it.
  void Detach() {
    if (handler_) {
      handler_->RemoveWatcher(this);
      handler_ = nullptr;
    }
    Forget(this);
  }

  // Tells the command where the chat stands now: what waits, or how it
  // ended when the agent is not at work.
  void Catch() {
    if (!handler_) {
      return;
    }
    if (const mojom::Approval* waiting = handler_->waiting()) {
      OnApproval(waiting);
    } else if (!handler_->working()) {
      const base::DictValue* last = handler_->last_line();
      const std::string* kind = last ? last->FindString("kind") : nullptr;
      const std::string* text = last ? last->FindString("text") : nullptr;
      last_kind_ = kind && *kind == "answer"   ? mojom::StepKind::kAnswer
                   : kind && *kind == "failed" ? mojom::StepKind::kFailed
                                               : mojom::StepKind::kStopped;
      last_text_ = text ? *text : std::string();
      End();
    }
  }

  // UwChatHandler::Watcher:
  void OnStep(const mojom::Step& step) override {
    base::DictValue sent;
    sent.Set("kind", KindName(step.kind));
    sent.Set("tool", step.tool);
    sent.Set("args", Parsed(step.args.empty() ? "{}" : step.args));
    sent.Set("result", Parsed(step.result.empty() ? "{}" : step.result));
    sent.Set("text", step.text);
    Notify("chat.step", base::Value(std::move(sent)));
    if (step.kind == mojom::StepKind::kAnswer ||
        step.kind == mojom::StepKind::kFailed ||
        step.kind == mojom::StepKind::kStoppedByUser ||
        step.kind == mojom::StepKind::kStopped) {
      last_kind_ = step.kind;
      last_text_ = step.text;
      // The agent stops working once its step has been told.
      base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
          FROM_HERE, base::BindOnce(&ChatSession::EndIfIdle,
                                    weak_factory_.GetWeakPtr()));
    }
  }

  void OnApproval(const mojom::Approval* approval) override {
    base::DictValue sent;
    sent.Set("approval", approval ? Described(*approval) : base::Value());
    Notify("chat.approval", base::Value(std::move(sent)));
  }

  void OnUsage(const mojom::ChatUsage& usage) override {
    if (usage.priced) {
      cost_ = usage.total;
    }
  }

  void OnGone() override {
    handler_->RemoveWatcher(this);
    handler_ = nullptr;
    last_kind_ = mojom::StepKind::kStopped;
    End();
  }

 private:
  void Notify(std::string_view method, base::Value params) {
    if (connection_) {
      params.GetDict().Set("chat", chat_);
      connection_->Notify(method, std::move(params));
    }
  }

  void EndIfIdle() {
    if (handler_ && !handler_->working() && !handler_->waiting()) {
      End();
    }
  }

  void End() {
    if (ended_) {
      return;
    }
    ended_ = true;
    std::string_view status = "stopped";
    if (last_kind_ == mojom::StepKind::kAnswer) {
      status = "answered";
    } else if (last_kind_ == mojom::StepKind::kFailed) {
      status = "failed";
    }
    base::DictValue ended;
    ended.Set("status", status);
    ended.Set("text", last_text_);
    ended.Set("cost", cost_ ? base::Value(*cost_) : base::Value());
    Notify("chat.end", base::Value(std::move(ended)));
    Detach();
  }

  // Ctrl-C, a closed terminal, or a killed caller stops the chat.
  void OnCommandGone() {
    if (handler_ && handler_->working()) {
      handler_->Stop();
    }
    Detach();
  }

  raw_ptr<UwChatHandler> handler_;
  const std::string chat_ = handler_->id();
  base::WeakPtr<CommandConnection> connection_;
  base::CallbackListSubscription closed_;
  std::optional<mojom::StepKind> last_kind_;
  std::string last_text_;
  std::optional<double> cost_;
  bool ended_ = false;
  base::WeakPtrFactory<ChatSession> weak_factory_{this};
};

void Forget(ChatSession* session) {
  std::erase_if(Sessions(),
                [&](const auto& owned) { return owned.get() == session; });
}

// Waits for the page of the chat `id` to open and read its chat, then gives
// `opened` its handler, or null after kOpenWait.
void WhenOpen(std::string id,
              base::OnceCallback<void(UwChatHandler*)> opened,
              base::TimeTicks since = base::TimeTicks::Now()) {
  UwChatHandler* handler = UwChatHandler::ForChat(id);
  if ((handler && handler->loaded()) ||
      base::TimeTicks::Now() - since > kOpenWait) {
    std::move(opened).Run(handler && handler->loaded() ? handler : nullptr);
    return;
  }
  base::SequencedTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE,
      base::BindOnce(&WhenOpen, std::move(id), std::move(opened), since),
      base::Milliseconds(50));
}

// Opens the chat `id` in the profile's run window, out of sight, as a
// scheduled chat's runs open their tabs.
bool OpenOutOfSight(Profile* profile, const std::string& id, bool started) {
  tabs::TabInterface* tab = profile ? OpenRunTab(profile) : nullptr;
  if (!tab) {
    return false;
  }
  if (started) {
    ChatStart start;
    start.from_terminal = true;
    SetChatStart(tab->GetContents(), std::move(start));
  }
  tab->GetContents()->GetController().LoadURL(
      ChatUrl(id), content::Referrer(), ui::PAGE_TRANSITION_AUTO_TOPLEVEL,
      std::string());
  return true;
}

std::optional<ApprovalMode> ModeFrom(const std::string* name) {
  if (!name) {
    return std::nullopt;
  }
  if (*name == "ask-first") {
    return ApprovalMode::kAskFirst;
  }
  if (*name == "full-auto") {
    return ApprovalMode::kFullAuto;
  }
  return ApprovalMode::kAuto;
}

UwChatHandler::CommandAsk AskFrom(const base::DictValue& params) {
  UwChatHandler::CommandAsk ask;
  if (const std::string* text = params.FindString("text")) {
    ask.text = *text;
  }
  ask.mode = ModeFrom(params.FindString("mode"));
  if (const std::string* model = params.FindString("model")) {
    ask.model = *model;
  }
  if (const std::string* effort = params.FindString("effort")) {
    ask.effort = *effort;
  }
  return ask;
}

// Attaches the command to `handler`, asks it what `params` says, and replies
// with the chat's id; its steps follow as notifications.
void Attach(base::Value id,
            base::DictValue params,
            CommandReply reply,
            base::WeakPtr<CommandConnection> connection,
            UwChatHandler* handler) {
  if (!handler || !connection) {
    std::move(reply).Run(
        CommandError(id, kCommandFailed, "uw couldn't open the chat."));
    return;
  }
  auto session = std::make_unique<ChatSession>(handler, connection);
  ChatSession* attached = session.get();
  Sessions().push_back(std::move(session));
  base::DictValue result;
  result.Set("chat", handler->id());
  std::move(reply).Run(CommandResult(id, base::Value(std::move(result))));
  UwChatHandler::CommandAsk ask = AskFrom(params);
  if (ask.text.empty()) {
    attached->Catch();
    return;
  }
  handler->AskFromCommand(std::move(ask));
}

// The open chat whose id is `prefix`, or starts with it.
UwChatHandler* OpenChat(std::string_view prefix) {
  UwChatHandler* found = nullptr;
  for (UwChatHandler* handler : UwChatHandler::Open()) {
    if (prefix.size() >= 4 && handler->id().starts_with(prefix)) {
      if (found) {
        return nullptr;
      }
      found = handler;
    }
  }
  return found;
}

// The saved chat of `index` whose id is `prefix`, or starts with it.
std::optional<std::string> SavedChat(const ChatIndex& index,
                                     std::string_view prefix) {
  std::optional<std::string> found;
  for (const ChatIndex::Entry& entry : index.Entries()) {
    if (prefix.size() >= 4 && entry.id.starts_with(prefix)) {
      if (found) {
        return std::nullopt;
      }
      found = entry.id;
    }
  }
  return found;
}

std::string_view StateName(ChatIndex::State state) {
  switch (state) {
    case ChatIndex::State::kDone:
      return "answered";
    case ChatIndex::State::kRunning:
      return "running";
    case ChatIndex::State::kWaiting:
      return "waiting";
    case ChatIndex::State::kStopped:
      return "stopped";
    case ChatIndex::State::kUnread:
      return "unread";
  }
}

void Start(CommandRequest request,
           CommandReply reply,
           base::WeakPtr<CommandConnection> connection,
           Profile* profile) {
  const std::string id = ChatFile::NewChatId();
  if (!OpenOutOfSight(profile, id, /*started=*/true)) {
    std::move(reply).Run(CommandError(request.id, kCommandFailed,
                                      "uw couldn't open a chat."));
    return;
  }
  WhenOpen(id, base::BindOnce(&Attach, request.id.Clone(),
                              std::move(request.params), std::move(reply),
                              std::move(connection)));
}

void Resume(CommandRequest request,
            CommandReply reply,
            base::WeakPtr<CommandConnection> connection,
            Profile* profile) {
  ChatIndex* index = profile ? ChatIndex::For(profile) : nullptr;
  const std::string* asked = request.params.FindString("chat");
  std::optional<std::string> id;
  if (index && asked) {
    id = SavedChat(*index, *asked);
  } else if (index && !index->Entries().empty()) {
    id = index->Entries().front().id;
  }
  if (!id) {
    std::move(reply).Run(CommandError(
        request.id, kCommandUsage,
        asked ? base::StrCat({"No chat is called ", *asked, ".\n  uw chats"})
              : std::string("This workspace has no chats yet.")));
    return;
  }
  if (!UwChatHandler::ForChat(*id) &&
      !OpenOutOfSight(profile, *id, /*started=*/false)) {
    std::move(reply).Run(CommandError(request.id, kCommandFailed,
                                      "uw couldn't open the chat."));
    return;
  }
  WhenOpen(*id, base::BindOnce(&Attach, request.id.Clone(),
                               std::move(request.params), std::move(reply),
                               std::move(connection)));
}

void List(CommandRequest request, CommandReply reply, Profile* profile) {
  ChatIndex* index = profile ? ChatIndex::For(profile) : nullptr;
  base::ListValue chats;
  for (const ChatIndex::Entry& entry :
       index ? index->Entries() : std::vector<ChatIndex::Entry>()) {
    chats.Append(base::DictValue()
                     .Set("id", entry.id)
                     .Set("title", entry.title)
                     .Set("state", StateName(entry.state))
                     .Set("fromTerminal", entry.from_terminal)
                     .Set("updated", entry.updated.InMillisecondsFSinceUnixEpoch()));
  }
  base::DictValue result;
  result.Set("chats", std::move(chats));
  std::move(reply).Run(CommandResult(request.id, base::Value(std::move(result))));
}

// Requests about a chat an open tab shows: answering what waits, stopping,
// steering, and letting go.
void AboutOpenChat(CommandRequest request, CommandReply reply) {
  const std::string* asked = request.params.FindString("chat");
  UwChatHandler* handler = asked ? OpenChat(*asked) : nullptr;
  if (!handler) {
    std::move(reply).Run(CommandError(
        request.id, kCommandUsage,
        base::StrCat({"No chat called ", asked ? *asked : "",
                      " is open in uw.\n  uw chats"})));
    return;
  }
  const std::string& method = request.method;
  base::DictValue result;
  result.Set("chat", handler->id());
  if (method == "chats.waiting") {
    result.Set("waiting", handler->waiting()
                              ? Described(*handler->waiting())
                              : base::Value());
  } else if (method == "chats.approve") {
    if (!handler->waiting()) {
      std::move(reply).Run(CommandError(
          request.id, kCommandUsage,
          base::StrCat({"Nothing in ", handler->id().substr(0, 8),
                        " waits for approval."})));
      return;
    }
    const std::string* choice = request.params.FindString("choice");
    const std::string* note = request.params.FindString("note");
    mojom::ApprovalChoice picked = mojom::ApprovalChoice::kDecline;
    if (choice && *choice == "allow") {
      picked = mojom::ApprovalChoice::kApprove;
    } else if (choice && *choice == "allow-for-chat") {
      picked = mojom::ApprovalChoice::kAllowForChat;
    } else if (choice && *choice == "always-allow") {
      picked = mojom::ApprovalChoice::kAlwaysAllow;
    } else if (choice && *choice == "stop") {
      picked = mojom::ApprovalChoice::kStop;
    }
    handler->AnswerApproval(picked, note ? *note : std::string());
  } else if (method == "chats.stop") {
    handler->Stop();
  } else if (method == "chats.steer") {
    const std::string* text = request.params.FindString("text");
    UwChatHandler::CommandAsk ask;
    ask.text = text ? *text : std::string();
    handler->AskFromCommand(std::move(ask));
  } else if (method == "chats.detach") {
    for (const std::unique_ptr<ChatSession>& session : Sessions()) {
      if (session->chat() == handler->id()) {
        session->Detach();
        break;
      }
    }
  } else {
    std::move(reply).Run(CommandError(
        request.id, kMethodNotFound,
        base::StrCat({"This uw can't answer ", method,
                      ". Update uw, then try again."})));
    return;
  }
  std::move(reply).Run(CommandResult(request.id, base::Value(std::move(result))));
}

}  // namespace

void RunChatCommand(CommandRequest request,
                    CommandReply reply,
                    base::WeakPtr<CommandConnection> connection) {
  const std::string method = request.method;
  if (method != "chats.start" && method != "chats.resume" &&
      method != "chats.list") {
    AboutOpenChat(std::move(request), std::move(reply));
    return;
  }
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
             const std::string&) {
            if (!profile) {
              std::move(reply).Run(CommandError(
                  request.id, kCommandFailed, "uw couldn't open that workspace."));
            } else if (request.method == "chats.list") {
              List(std::move(request), std::move(reply), profile);
            } else if (!connection) {
              std::move(reply).Run(CommandError(request.id, kCommandFailed,
                                                "The command went away."));
            } else if (request.method == "chats.start") {
              Start(std::move(request), std::move(reply), std::move(connection),
                    profile);
            } else {
              Resume(std::move(request), std::move(reply),
                     std::move(connection), profile);
            }
          },
          std::move(request), std::move(reply), std::move(connection)));
}

}  // namespace uw
