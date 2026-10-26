// Copyright 2026 The uw Authors. All rights reserved.

#ifndef UW_COMPONENTS_TERMINAL_SHELL_SESSION_H_
#define UW_COMPONENTS_TERMINAL_SHELL_SESSION_H_

#include <string>

#include "base/files/file_path.h"
#include "base/functional/callback.h"
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
    // Nothing follows. `started` is false for a shell that could not start.
    virtual void OnShellExited(bool started) = 0;

   protected:
    virtual ~Client() = default;
  };

  // `options` runs on the shell's sequence, where reading the user's account
  // holds nothing up. `client` outlives the session.
  ShellSession(base::OnceCallback<ShellHost::Options()> options,
               Client* client);
  ShellSession(const ShellSession&) = delete;
  ShellSession& operator=(const ShellSession&) = delete;
  ~ShellSession();

  // Sends `bytes` to the shell as if typed. What the shell cannot take yet is
  // kept and sent when it can.
  void Write(std::string bytes);
  void Resize(int columns, int rows);
  void AckOutput();
  // Answers with what the terminal waits for. A shell that has not started,
  // or has ended, answers with nothing.
  void GetActivity(base::OnceCallback<void(ShellHost::Activity)> done);

 private:
  class Core;

  void OnOutput(std::string text);
  void OnFolderChanged(const base::FilePath& folder);
  void OnExited(bool started);

  const raw_ptr<Client> client_;
  base::SequenceBound<Core> core_;
  base::WeakPtrFactory<ShellSession> weak_factory_{this};
};

}  // namespace uw

#endif  // UW_COMPONENTS_TERMINAL_SHELL_SESSION_H_
