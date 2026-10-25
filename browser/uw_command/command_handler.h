// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#ifndef UW_BROWSER_UW_COMMAND_COMMAND_HANDLER_H_
#define UW_BROWSER_UW_COMMAND_COMMAND_HANDLER_H_

#include <string>

#include "base/functional/callback_forward.h"

namespace uw {

struct CommandRequest;

// Gets a reply line to send back.
using CommandReply = base::OnceCallback<void(std::string line)>;

// Answers one request from the uw command, on the UI thread.
void HandleCommand(CommandRequest request, CommandReply reply);

// uw settings: list, get, and set. See docs/plans/uw-command.md#settings.
void RunSettingsCommand(CommandRequest request, CommandReply reply);

// uw connectors, also uw mcp: list, add, remove, and sign in. See
// docs/plans/uw-command.md#connectors.
void RunConnectorsCommand(CommandRequest request, CommandReply reply);

}  // namespace uw

#endif  // UW_BROWSER_UW_COMMAND_COMMAND_HANDLER_H_
