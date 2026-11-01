// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/components/terminal/shell_host.h"

#include <string>
#include <string_view>

#include "base/strings/string_util.h"

namespace uw {

// static
bool ShellHost::IsPasswordPrompt(std::string_view line) {
  const std::string_view asked =
      base::TrimWhitespaceASCII(line, base::TRIM_TRAILING);
  if (!asked.ends_with(':')) {
    return false;
  }
  const std::string lower = base::ToLowerASCII(asked);
  return lower.find("password") != std::string::npos ||
         lower.find("passphrase") != std::string::npos;
}

}  // namespace uw
