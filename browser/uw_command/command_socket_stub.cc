// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/browser/uw_command/command_socket.h"

#include <utility>

// The command talks to uw over a Unix socket, which only POSIX systems have.
// Windows gets a named pipe; see docs/plans/windows.md.

namespace uw {

struct CommandSocket::Connection {};

CommandSocket::CommandSocket(const base::FilePath& path,
                             Received received,
                             Closed closed)
    : received_(std::move(received)), closed_(std::move(closed)) {}

CommandSocket::~CommandSocket() = default;

void CommandSocket::Send(int connection, std::string text) {}

}  // namespace uw
