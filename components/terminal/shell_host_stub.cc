// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/components/terminal/shell_host.h"

// Terminal tabs run a shell on the Mac only so far. Windows gets ConPTY; see
// docs/plans/windows.md.

namespace uw {

// static
ShellHost::Options ShellHost::ForUser() {
  return Options();
}

// static
std::unique_ptr<ShellHost> ShellHost::Start(const Options& options) {
  return nullptr;
}

ShellHost::~ShellHost() = default;

size_t ShellHost::Write(std::string_view bytes) {
  return 0;
}

bool ShellHost::Resize(int columns, int rows) {
  return false;
}

std::optional<std::string> ShellHost::RunningCommand() const {
  return std::nullopt;
}

ShellHost::Activity ShellHost::CurrentActivity() const {
  return Activity();
}

base::FilePath ShellHost::Folder() const {
  return base::FilePath();
}

}  // namespace uw
