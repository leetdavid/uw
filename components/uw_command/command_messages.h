// Copyright 2026 The uw Authors. All rights reserved.

#ifndef UW_COMPONENTS_UW_COMMAND_COMMAND_MESSAGES_H_
#define UW_COMPONENTS_UW_COMMAND_COMMAND_MESSAGES_H_

#include <optional>
#include <string>
#include <string_view>

#include "base/values.h"

namespace uw {

// The uw command talks to uw in JSON-RPC 2.0, one message per line. An error's
// code is the command's exit code: failed, wrong usage, or needs you. See
// docs/plans/uw-command.md.
inline constexpr int kCommandFailed = 1;
inline constexpr int kCommandUsage = 2;
inline constexpr int kCommandNeedsYou = 3;
inline constexpr int kParseError = -32700;
inline constexpr int kMethodNotFound = -32601;

struct CommandRequest {
  CommandRequest();
  CommandRequest(CommandRequest&&);
  CommandRequest& operator=(CommandRequest&&);
  ~CommandRequest();

  base::Value id;
  std::string method;
  base::DictValue params;
};

// One request, or nothing for a line that is not one.
std::optional<CommandRequest> ParseCommandRequest(std::string_view line);

// A reply, ending in a newline.
std::string CommandResult(const base::Value& id, base::Value result);
std::string CommandError(const base::Value& id,
                         int code,
                         std::string_view message);

// A notification uw sends without being asked, such as a chat's next step.
std::string CommandNotification(std::string_view method, base::Value params);

}  // namespace uw

#endif  // UW_COMPONENTS_UW_COMMAND_COMMAND_MESSAGES_H_
