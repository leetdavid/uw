// Copyright 2026 The uw Authors. All rights reserved.

#ifndef UW_BROWSER_UW_COMMAND_COMMAND_HANDLER_H_
#define UW_BROWSER_UW_COMMAND_COMMAND_HANDLER_H_

#include <optional>
#include <string>

#include "base/functional/callback_forward.h"
#include "base/memory/weak_ptr.h"
#include "base/values.h"

namespace uw {

namespace mojom {
class Approval;
}

enum class ApprovalMode;
class CommandConnection;
struct CommandRequest;

// Gets a reply line to send back.
using CommandReply = base::OnceCallback<void(std::string line)>;

// Answers one request from the uw command, on the UI thread. A chat keeps
// `connection` to send its steps on.
void HandleCommand(CommandRequest request,
                   CommandReply reply,
                   base::WeakPtr<CommandConnection> connection);

// uw settings: list, get, and set. See docs/plans/uw-command.md#settings.
void RunSettingsCommand(CommandRequest request, CommandReply reply);

// uw connectors, also uw mcp: list, add, remove, and sign in. See
// docs/plans/uw-command.md#connectors.
void RunConnectorsCommand(CommandRequest request, CommandReply reply);

// uw chat, uw chats, and uw approve: start, pick up, answer, stop, and list
// chats. See docs/plans/uw-command.md#chats.
void RunChatCommand(CommandRequest request,
                    CommandReply reply,
                    base::WeakPtr<CommandConnection> connection);

// An approval as the command reads it.
base::Value DescribedApproval(const mojom::Approval& approval);

// The approval mode the command calls `name`, such as "ask-first": nothing
// without a name, and auto for a name it doesn't know.
std::optional<ApprovalMode> CommandModeFrom(const std::string* name);

// uw memory: list and forget agent memory. See
// docs/plans/agent-memory.md#the-uw-command.
void RunMemoryCommand(CommandRequest request, CommandReply reply);

// uw tabs: list, show, move, and close the user's tabs, and rename tasks,
// without approvals. See docs/plans/tab-tools.md#who-gets-the-tools.
void RunTabsCommand(CommandRequest request, CommandReply reply);

// uw mcp serve: the page tools an outside agent uses, out of sight, with the
// user's approval. See docs/plans/uw-command.md#the-tool-server.
void RunToolsCommand(CommandRequest request,
                     CommandReply reply,
                     base::WeakPtr<CommandConnection> connection);

}  // namespace uw

#endif  // UW_BROWSER_UW_COMMAND_COMMAND_HANDLER_H_
