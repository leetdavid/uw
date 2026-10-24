// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/browser/uw_command/command_channel.h"

#include <optional>
#include <utility>

#include "base/apple/bundle_locations.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/no_destructor.h"
#include "base/path_service.h"
#include "base/task/bind_post_task.h"
#include "base/task/thread_pool.h"
#include "chrome/common/chrome_paths.h"
#include "content/public/browser/browser_task_traits.h"
#include "content/public/browser/browser_thread.h"
#include "uw/browser/uw_command/command_handler.h"
#include "uw/browser/uw_command/command_install.h"
#include "uw/browser/uw_command/command_socket.h"
#include "uw/components/uw_command/command_messages.h"

namespace uw {

namespace {

// A socket left by a uw that crashed would keep the new one from binding.
bool ClearSocket(const base::FilePath& socket) {
  return base::DeleteFile(socket);
}

}  // namespace

// static
CommandChannel& CommandChannel::Get() {
  static base::NoDestructor<CommandChannel> channel;
  return *channel;
}

CommandChannel::CommandChannel() = default;
CommandChannel::~CommandChannel() = default;

void CommandChannel::Start() {
  base::FilePath user_data;
  if (!base::PathService::Get(chrome::DIR_USER_DATA, &user_data)) {
    return;
  }
  const base::FilePath socket = user_data.Append(kCommandSocketName);
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::MayBlock()},
      base::BindOnce(
          [](const base::FilePath& socket) {
            return LinksInto(base::FilePath(kCommandLink),
                             base::apple::OuterBundlePath()) &&
                   ClearSocket(socket);
          },
          socket),
      base::BindOnce(
          [](base::WeakPtr<CommandChannel> channel,
             const base::FilePath& socket, bool installed) {
            if (channel && installed) {
              channel->Open(socket);
            }
          },
          weak_factory_.GetWeakPtr(), socket));
}

void CommandChannel::OpenForTesting(const base::FilePath& socket) {
  ClearSocket(socket);
  Open(socket);
}

void CommandChannel::Close() {
  socket_.Reset();
}

void CommandChannel::Open(const base::FilePath& socket) {
  socket_ = base::SequenceBound<CommandSocket>(
      content::GetIOThreadTaskRunner({}), socket,
      base::BindPostTaskToCurrentDefault(base::BindRepeating(
          &CommandChannel::Received, weak_factory_.GetWeakPtr())));
}

void CommandChannel::Received(int connection, std::string line) {
  CommandReply reply = base::BindOnce(&CommandChannel::Send,
                                      weak_factory_.GetWeakPtr(), connection);
  std::optional<CommandRequest> request = ParseCommandRequest(line);
  if (!request) {
    std::move(reply).Run(CommandError(base::Value(), kParseError,
                                      "uw can't read that request."));
    return;
  }
  HandleCommand(std::move(*request), std::move(reply));
}

void CommandChannel::Send(int connection, std::string line) {
  socket_.AsyncCall(&CommandSocket::Send).WithArgs(connection, std::move(line));
}

}  // namespace uw
