// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#ifndef UW_BROWSER_CONNECTORS_LOCAL_TRANSPORT_H_
#define UW_BROWSER_CONNECTORS_LOCAL_TRANSPORT_H_

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "base/environment.h"
#include "base/files/file_path.h"
#include "base/functional/callback.h"
#include "base/memory/weak_ptr.h"
#include "base/threading/sequence_bound.h"
#include "uw/browser/connectors/mcp_client.h"
#include "uw/components/connectors/connector_config.h"

namespace uw {

// A connector that is a program on this Mac. uw starts it with the
// environment of the user's terminal, so it finds the programs their
// terminal finds, and talks to it one line of JSON at a time over its input
// and output. Its error output goes to `log`. Destroying the transport
// closes its input, which tells the program to quit, and ends it if it
// doesn't.
class LocalTransport : public McpTransport {
 public:
  using OnMessage = base::RepeatingCallback<void(const mcp::Message&)>;
  // Gets the last line the program wrote to its error output that names an
  // error, or else its last line, or why it could not start.
  using OnExit = base::OnceCallback<void(std::string)>;

  // A shell runs `exec "$0" "$@"` with the command and its arguments, so
  // nothing in them is read by the shell. With `environment`, the
  // terminal's, that is /bin/sh; without it, `shell` as a login shell.
  LocalTransport(const ConnectorConfig& connector,
                 const base::FilePath& shell,
                 const std::optional<base::EnvironmentMap>& environment,
                 const base::FilePath& log,
                 OnMessage on_notification,
                 OnExit on_exit);
  LocalTransport(const LocalTransport&) = delete;
  LocalTransport& operator=(const LocalTransport&) = delete;
  ~LocalTransport() override;

  // McpTransport:
  void Send(int id,
            std::string_view version,
            mcp::Era era,
            std::string_view method,
            std::string_view name,
            std::string body,
            Done done) override;
  void Notify(std::string body) override;
  void Forget(int id) override;

 private:
  class Pipes;

  void OnLine(std::string line);
  void OnStopped(std::string last_error);

  base::SequenceBound<Pipes> pipes_;
  OnMessage on_notification_;
  OnExit on_exit_;
  std::map<int, Done> pending_;
  bool stopped_ = false;
  base::WeakPtrFactory<LocalTransport> weak_factory_{this};
};

}  // namespace uw

#endif  // UW_BROWSER_CONNECTORS_LOCAL_TRANSPORT_H_
