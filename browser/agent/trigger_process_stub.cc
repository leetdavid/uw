// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/browser/agent/trigger_process.h"

#include <utility>

#include "base/functional/bind.h"
#include "base/task/sequenced_task_runner.h"

// A trigger's command runs on POSIX pipes. Starting one on Windows comes
// later; see docs/plans/windows.md.

namespace uw {

class TriggerProcess::Pipe {};

TriggerProcess::TriggerProcess(const std::string& command,
                               const base::FilePath& folder,
                               OnLine on_line,
                               OnEnd on_end) {
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(std::move(on_end), -1));
}

TriggerProcess::~TriggerProcess() = default;

}  // namespace uw
