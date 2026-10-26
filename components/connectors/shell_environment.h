// Copyright 2026 The uw Authors. All rights reserved.

#ifndef UW_COMPONENTS_CONNECTORS_SHELL_ENVIRONMENT_H_
#define UW_COMPONENTS_CONNECTORS_SHELL_ENVIRONMENT_H_

#include <optional>
#include <string>
#include <string_view>

#include "base/environment.h"

namespace uw {

// What an interactive login shell runs to print its environment between two
// marks, so that whatever the user's startup files print around it is left
// out.
std::string ShellEnvironmentCommand();

// The environment in what ShellEnvironmentCommand printed. Nothing when the
// marks or PATH are missing.
std::optional<base::EnvironmentMap> ParseShellEnvironment(
    std::string_view output);

}  // namespace uw

#endif  // UW_COMPONENTS_CONNECTORS_SHELL_ENVIRONMENT_H_
