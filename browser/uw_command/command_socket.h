// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#ifndef UW_BROWSER_UW_COMMAND_COMMAND_SOCKET_H_
#define UW_BROWSER_UW_COMMAND_COMMAND_SOCKET_H_

#include <map>
#include <memory>
#include <string>

#include "base/files/file_path.h"
#include "base/functional/callback.h"
#include "base/memory/weak_ptr.h"

namespace net {
class StreamSocket;
class UnixDomainServerSocket;
}  // namespace net

namespace uw {

// The uw command's socket. It lives on the IO thread, takes connections only
// from the user's own account, and hands each line it reads to `received`.
class CommandSocket {
 public:
  // A line without its newline, and the number of the connection it came on.
  using Received =
      base::RepeatingCallback<void(int connection, std::string line)>;

  CommandSocket(const base::FilePath& path, Received received);
  CommandSocket(const CommandSocket&) = delete;
  CommandSocket& operator=(const CommandSocket&) = delete;
  ~CommandSocket();

  // Sends `text` on `connection`, if it is still open.
  void Send(int connection, std::string text);

 private:
  struct Connection;

  void Accept();
  void Accepted(int result);
  // Takes the accepted connection. False when accepting failed.
  bool Adopt(int result);
  void Read(int connection);
  void DidRead(int connection, int result);
  // Takes in what a read gave. False once the connection is closed.
  bool TakeIn(int connection, int result);
  void Write(int connection);
  void DidWrite(int connection, int result);
  void Close(int connection);

  const Received received_;
  std::unique_ptr<net::UnixDomainServerSocket> server_;
  std::unique_ptr<net::StreamSocket> accepted_;
  std::map<int, std::unique_ptr<Connection>> connections_;
  int next_connection_ = 1;
  base::WeakPtrFactory<CommandSocket> weak_factory_{this};
};

}  // namespace uw

#endif  // UW_BROWSER_UW_COMMAND_COMMAND_SOCKET_H_
