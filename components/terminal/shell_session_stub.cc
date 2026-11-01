// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/components/terminal/shell_session.h"

#include <utility>

#include "base/functional/bind.h"
#include "base/task/sequenced_task_runner.h"

// The shell is read through POSIX file descriptors. Elsewhere it never
// starts, so the terminal tab says so; see docs/plans/windows.md.

namespace uw {

class ShellSession::Core {};

ShellSession::ShellSession(base::OnceCallback<ShellHost::Options()> options,
                           Client* client)
    : client_(client) {
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(&ShellSession::OnExited,
                                weak_factory_.GetWeakPtr(), false));
}

ShellSession::~ShellSession() = default;

void ShellSession::Write(std::string bytes) {}

void ShellSession::Resize(int columns, int rows) {}

void ShellSession::AckOutput() {}

void ShellSession::GetActivity(
    base::OnceCallback<void(ShellHost::Activity)> done) {
  std::move(done).Run(ShellHost::Activity());
}

void ShellSession::OnOutput(std::string text) {}

void ShellSession::OnFolderChanged(const base::FilePath& folder) {}

void ShellSession::OnExited(bool started) {
  client_->OnShellExited(started);
}

}  // namespace uw
