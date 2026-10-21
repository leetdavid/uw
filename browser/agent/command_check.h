// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#ifndef UW_BROWSER_AGENT_COMMAND_CHECK_H_
#define UW_BROWSER_AGENT_COMMAND_CHECK_H_

#include <memory>
#include <optional>
#include <string>

#include "base/functional/callback.h"
#include "uw/browser/ai_providers/models.h"
#include "uw/browser/ai_providers/remote_model_client.h"
#include "uw/components/agent/command_check.h"

class Profile;

namespace uw {

// Sends the command check of `input` to the decision model, or to the
// language model when there is none, or else to `chat_model` with `chat_key`.
// `done` gets what the answer says, or nothing for no answer. Destroying the
// client drops the check. Null when no model can take it. See
// docs/plans/agent-chats.md.
std::unique_ptr<RemoteModelClient> SendCommandCheck(
    Profile& profile,
    const AiConnectionConfig& chat_model,
    const std::string& chat_key,
    const CommandCheckInput& input,
    base::OnceCallback<void(std::optional<CommandCheckAnswer>)> done);

}  // namespace uw

#endif  // UW_BROWSER_AGENT_COMMAND_CHECK_H_
