// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/browser/uw_command/command_handler.h"

#include <utility>

#include "base/functional/callback.h"
#include "base/strings/strcat.h"
#include "base/values.h"
#include "components/version_info/version_info.h"
#include "uw/components/uw_command/command_messages.h"

namespace uw {

void HandleCommand(CommandRequest request,
                   CommandReply reply,
                   base::WeakPtr<CommandConnection> connection) {
  if (request.method == "hello") {
    base::DictValue hello;
    hello.Set("version", version_info::GetVersionNumber());
    std::move(reply).Run(
        CommandResult(request.id, base::Value(std::move(hello))));
    return;
  }
  if (request.method.starts_with("settings.")) {
    RunSettingsCommand(std::move(request), std::move(reply));
    return;
  }
  if (request.method.starts_with("connectors.")) {
    RunConnectorsCommand(std::move(request), std::move(reply));
    return;
  }
  if (request.method.starts_with("chats.")) {
    RunChatCommand(std::move(request), std::move(reply), std::move(connection));
    return;
  }
  if (request.method.starts_with("memory.")) {
    RunMemoryCommand(std::move(request), std::move(reply));
    return;
  }
  if (request.method.starts_with("tabs.")) {
    RunTabsCommand(std::move(request), std::move(reply));
    return;
  }
  if (request.method.starts_with("tools.")) {
    RunToolsCommand(std::move(request), std::move(reply),
                    std::move(connection));
    return;
  }
  std::move(reply).Run(CommandError(
      request.id, kMethodNotFound,
      base::StrCat({"This uw can't answer ", request.method,
                    ". Update uw, then try again."})));
}

}  // namespace uw
