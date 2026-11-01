// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/browser/connectors/local_transport.h"

#include <utility>

#include "base/functional/bind.h"
#include "base/task/sequenced_task_runner.h"

// A local connector's program runs on POSIX pipes. Starting one on Windows
// comes later; see docs/plans/windows.md.

namespace uw {
namespace {

constexpr char kNotYet[] =
    "Connectors that run on this computer work on the Mac only so far.";

}  // namespace

class LocalTransport::Pipes {};

LocalTransport::LocalTransport(
    const ConnectorConfig& connector,
    const base::FilePath& shell,
    const std::optional<base::EnvironmentMap>& environment,
    const base::FilePath& log,
    OnMessage on_notification,
    OnExit on_exit)
    : on_notification_(std::move(on_notification)),
      on_exit_(std::move(on_exit)) {
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(&LocalTransport::OnStopped,
                                weak_factory_.GetWeakPtr(), kNotYet));
}

LocalTransport::~LocalTransport() = default;

void LocalTransport::Send(int id,
                          std::string_view version,
                          mcp::Era era,
                          std::string_view method,
                          std::string_view name,
                          std::string body,
                          Done done) {
  std::move(done).Run(McpReply::Problem(kNotYet));
}

void LocalTransport::Notify(std::string body) {}

void LocalTransport::Forget(int id) {}

void LocalTransport::OnLine(std::string line) {}

void LocalTransport::OnStopped(std::string last_error) {
  stopped_ = true;
  if (on_exit_) {
    std::move(on_exit_).Run(std::move(last_error));
  }
}

}  // namespace uw
