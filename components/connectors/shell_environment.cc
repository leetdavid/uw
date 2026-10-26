// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/components/connectors/shell_environment.h"

#include "base/strings/strcat.h"
#include "base/strings/string_split.h"

namespace uw {
namespace {

constexpr std::string_view kMark = "__uw_environment_7d1f__";

}  // namespace

std::string ShellEnvironmentCommand() {
  return base::StrCat({"printf '%s' '", kMark,
                       "'; /usr/bin/env -0; printf '%s' '", kMark, "'"});
}

std::optional<base::EnvironmentMap> ParseShellEnvironment(
    std::string_view output) {
  const size_t start = output.find(kMark);
  const size_t end = start == std::string_view::npos
                         ? start
                         : output.find(kMark, start + kMark.size());
  if (end == std::string_view::npos) {
    return std::nullopt;
  }
  base::EnvironmentMap environment;
  for (std::string_view entry : base::SplitStringPiece(
           output.substr(start + kMark.size(), end - start - kMark.size()),
           std::string_view("\0", 1), base::KEEP_WHITESPACE,
           base::SPLIT_WANT_NONEMPTY)) {
    const size_t equals = entry.find('=');
    if (equals != std::string_view::npos && equals > 0) {
      environment[std::string(entry.substr(0, equals))] =
          std::string(entry.substr(equals + 1));
    }
  }
  if (!environment.contains("PATH")) {
    return std::nullopt;
  }
  return environment;
}

}  // namespace uw
