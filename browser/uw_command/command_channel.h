// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#ifndef UW_BROWSER_UW_COMMAND_COMMAND_CHANNEL_H_
#define UW_BROWSER_UW_COMMAND_COMMAND_CHANNEL_H_

#include <string>

#include "base/files/file_path.h"
#include "base/memory/weak_ptr.h"
#include "base/threading/sequence_bound.h"

namespace uw {

class CommandSocket;

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
  void Send(int connection, std::string line);

  base::SequenceBound<CommandSocket> socket_;
  base::WeakPtrFactory<CommandChannel> weak_factory_{this};
};

}  // namespace uw

#endif  // UW_BROWSER_UW_COMMAND_COMMAND_CHANNEL_H_
