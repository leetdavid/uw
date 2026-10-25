// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#ifndef UW_BROWSER_UW_COMMAND_COMMAND_CHANNEL_H_
#define UW_BROWSER_UW_COMMAND_COMMAND_CHANNEL_H_

#include <map>
#include <memory>
#include <string>
#include <string_view>

#include "base/callback_list.h"
#include "base/values.h"

#include "base/files/file_path.h"
#include "base/memory/weak_ptr.h"
#include "base/threading/sequence_bound.h"

namespace uw {

class CommandSocket;

// One connection from the uw command, open while the command runs. A chat
// sends it steps as they happen, and stops when the command goes away.
class CommandConnection {
 public:
  using Sender = base::RepeatingCallback<void(std::string line)>;

  explicit CommandConnection(Sender send);
  CommandConnection(const CommandConnection&) = delete;
  CommandConnection& operator=(const CommandConnection&) = delete;
  // Runs what OnClose was given.
  ~CommandConnection();

  void Send(std::string line);
  void Notify(std::string_view method, base::Value params);
  [[nodiscard]] base::CallbackListSubscription OnClose(base::OnceClosure closed);
  base::WeakPtr<CommandConnection> GetWeakPtr() {
    return weak_factory_.GetWeakPtr();
  }

 private:
  const Sender send_;
  base::OnceClosureList closed_;
  base::WeakPtrFactory<CommandConnection> weak_factory_{this};
};

// The file name of the uw command's socket in the user data folder.
inline constexpr char kCommandSocketName[] = "uw-command.sock";

// The channel the uw command reaches uw by: a socket in the user data folder,
// open while the command is installed. See docs/plans/uw-command.md.
class CommandChannel {
 public:
  static CommandChannel& Get();

  CommandChannel();
  CommandChannel(const CommandChannel&) = delete;
  CommandChannel& operator=(const CommandChannel&) = delete;
  ~CommandChannel();

  // Opens the channel if the command is installed and it isn't open yet.
  void Start();
  // Opens it at `socket` at once.
  void OpenForTesting(const base::FilePath& socket);
  void Close();
  bool open() const { return !socket_.is_null(); }

 private:
  void Open(const base::FilePath& socket);
  void Received(int connection, std::string line);
  void Closed(int connection);
  void Send(int connection, std::string line);

  base::SequenceBound<CommandSocket> socket_;
  std::map<int, std::unique_ptr<CommandConnection>> connections_;
  base::WeakPtrFactory<CommandChannel> weak_factory_{this};
};

}  // namespace uw

#endif  // UW_BROWSER_UW_COMMAND_COMMAND_CHANNEL_H_
