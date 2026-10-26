// Copyright 2026 The uw Authors. All rights reserved.

#ifndef UW_COMPONENTS_TERMINAL_SHELL_QUOTE_H_
#define UW_COMPONENTS_TERMINAL_SHELL_QUOTE_H_

#include <string>
#include <vector>

#include "base/files/file_path.h"

namespace uw {

// The paths as the shell should read them, separated by spaces. Like Ghostty,
// it puts a backslash before each character the shell would treat specially.
std::string QuotePathsForShell(const std::vector<base::FilePath>& paths);

}  // namespace uw

#endif  // UW_COMPONENTS_TERMINAL_SHELL_QUOTE_H_
