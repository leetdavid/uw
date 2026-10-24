// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/browser/uw_command/command_socket.h"

#include <unistd.h>

#include <utility>

#include "base/functional/bind.h"
#include "base/logging.h"
#include "net/base/io_buffer.h"
#include "net/base/net_errors.h"
#include "net/socket/stream_socket.h"
#include "net/socket/unix_domain_server_socket_posix.h"
#include "net/traffic_annotation/network_traffic_annotation.h"

namespace uw {

namespace {

constexpr net::NetworkTrafficAnnotationTag kTrafficAnnotation =
    net::DefineNetworkTrafficAnnotation("uw_command", R"(
      semantics {
        sender: "uw command"
        description:
          "Answers the uw command, a program on this Mac that the user "
          "installed, over a socket in uw's user data folder."
        trigger: "The user, a script, or an agent runs the uw command."
        data: "Settings and the answers to the command's requests."
        destination: LOCAL
      }
      policy {
        cookies_allowed: NO
        setting: "Uninstall the uw command in Settings, System."
        policy_exception_justification: "Not implemented."
      })");

constexpr int kReadSize = 64 * 1024;
// A request longer than this closes its connection.
constexpr size_t kLongestLine = 4 * 1024 * 1024;

bool FromThisUser(const net::UnixDomainServerSocket::Credentials& peer) {
  return peer.user_id == geteuid();
}

}  // namespace

struct CommandSocket::Connection {
  std::unique_ptr<net::StreamSocket> socket;
  scoped_refptr<net::IOBufferWithSize> read =
      base::MakeRefCounted<net::IOBufferWithSize>(kReadSize);
  // What came after the last newline.
  std::string partial;
  // Text waiting for the write under way to finish.
  std::string waiting;
  scoped_refptr<net::DrainableIOBuffer> writing;
};

CommandSocket::CommandSocket(const base::FilePath& path, Received received)
    : received_(std::move(received)),
      server_(std::make_unique<net::UnixDomainServerSocket>(
          base::BindRepeating(&FromThisUser),
          /*use_abstract_namespace=*/false)) {
  const int result = server_->BindAndListen(path.value(), /*backlog=*/8);
  if (result != net::OK) {
    LOG(ERROR) << "The uw command's socket can't listen at " << path << ": "
               << net::ErrorToString(result);
    server_.reset();
    return;
  }
  Accept();
}

CommandSocket::~CommandSocket() = default;

void CommandSocket::Send(int connection, std::string text) {
  auto found = connections_.find(connection);
  if (found == connections_.end()) {
    return;
  }
  found->second->waiting += text;
  if (!found->second->writing) {
    Write(connection);
  }
}

void CommandSocket::Accept() {
  while (server_) {
    const int result = server_->Accept(
        &accepted_, base::BindOnce(&CommandSocket::Accepted,
                                   weak_factory_.GetWeakPtr()));
    if (result == net::ERR_IO_PENDING || !Adopt(result)) {
      return;
    }
  }
}

void CommandSocket::Accepted(int result) {
  if (Adopt(result)) {
    Accept();
  }
}

bool CommandSocket::Adopt(int result) {
  if (result != net::OK) {
    LOG(ERROR) << "The uw command's socket stopped accepting: "
               << net::ErrorToString(result);
    return false;
  }
  const int connection = next_connection_++;
  auto open = std::make_unique<Connection>();
  open->socket = std::move(accepted_);
  connections_[connection] = std::move(open);
  Read(connection);
  return true;
}

void CommandSocket::Read(int connection) {
  while (connections_.contains(connection)) {
    Connection& open = *connections_[connection];
    const int result = open.socket->Read(
        open.read.get(), open.read->size(),
        base::BindOnce(&CommandSocket::DidRead, weak_factory_.GetWeakPtr(),
                       connection));
    if (result == net::ERR_IO_PENDING || !TakeIn(connection, result)) {
      return;
    }
  }
}

void CommandSocket::DidRead(int connection, int result) {
  if (TakeIn(connection, result)) {
    Read(connection);
  }
}

bool CommandSocket::TakeIn(int connection, int result) {
  if (result <= 0) {
    Close(connection);
    return false;
  }
  Connection& open = *connections_[connection];
  open.partial.append(open.read->data(), result);
  for (size_t end = open.partial.find('\n'); end != std::string::npos;
       end = open.partial.find('\n')) {
    received_.Run(connection, open.partial.substr(0, end));
    open.partial.erase(0, end + 1);
  }
  if (open.partial.size() > kLongestLine) {
    Close(connection);
    return false;
  }
  return true;
}

void CommandSocket::Write(int connection) {
  while (connections_.contains(connection)) {
    Connection& open = *connections_[connection];
    if (!open.writing) {
      if (open.waiting.empty()) {
        return;
      }
      auto text = base::MakeRefCounted<net::StringIOBuffer>(
          std::exchange(open.waiting, std::string()));
      const int size = text->size();
      open.writing =
          base::MakeRefCounted<net::DrainableIOBuffer>(std::move(text), size);
    }
    const int result = open.socket->Write(
        open.writing.get(), open.writing->BytesRemaining(),
        base::BindOnce(&CommandSocket::DidWrite, weak_factory_.GetWeakPtr(),
                       connection),
        kTrafficAnnotation);
    if (result == net::ERR_IO_PENDING) {
      return;
    }
    if (result < 0) {
      Close(connection);
      return;
    }
    open.writing->DidConsume(result);
    if (open.writing->BytesRemaining() == 0) {
      open.writing = nullptr;
    }
  }
}

void CommandSocket::DidWrite(int connection, int result) {
  auto found = connections_.find(connection);
  if (found == connections_.end()) {
    return;
  }
  if (result < 0) {
    Close(connection);
    return;
  }
  Connection& open = *found->second;
  open.writing->DidConsume(result);
  if (open.writing->BytesRemaining() == 0) {
    open.writing = nullptr;
  }
  Write(connection);
}

void CommandSocket::Close(int connection) {
  connections_.erase(connection);
}

}  // namespace uw
