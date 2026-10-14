// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#ifndef UW_COMPONENTS_TERMINAL_SHELL_SESSION_H_
#define UW_COMPONENTS_TERMINAL_SHELL_SESSION_H_

#include <string>

#include "base/files/file_path.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/threading/sequence_bound.h"
#include "uw/components/terminal/shell_host.h"

namespace uw {

// The shell behind one terminal tab. The shell is started, read, written, and
// ended on a sequence of its own, so nothing here blocks the caller. The
// client is called on the sequence that made the session.
class ShellSession {
 public:
  class Client {
   public:
    // What the shell printed, cut between whole UTF-8 characters. Nothing more
    // arrives until AckOutput(), so a shell that prints faster than the client
    // draws is slowed down instead of filling memory.
    virtual void OnShellOutput(std::string text) = 0;
    virtual void OnShellFolderChanged(const base::FilePath& folder) = 0;
    // The shell has ended or could not start. Nothing follows.
    virtual void OnShellExited() = 0;

   protected:
    virtual ~Client() = default;
  };

  // `client` outlives the session.
  ShellSession(const ShellHost::Options& options, Client* client);
  ShellSession(const ShellSession&) = delete;
  ShellSession& operator=(const ShellSession&) = delete;
  ~ShellSession();

  // Sends `bytes` to the shell as if typed. What the shell cannot take yet is
  // kept and sent when it can.
  void Write(std::string bytes);
  void Resize(int columns, int rows);
  void AckOutput();

 private:
  class Core;

  void OnOutput(std::string text);
  void OnFolderChanged(const base::FilePath& folder);
  void OnExited();

  const raw_ptr<Client> client_;
  base::SequenceBound<Core> core_;
  base::WeakPtrFactory<ShellSession> weak_factory_{this};
};

}  // namespace uw

#endif  // UW_COMPONENTS_TERMINAL_SHELL_SESSION_H_
