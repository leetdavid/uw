// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#ifndef UW_COMPONENTS_TERMINAL_SHELL_HOST_H_
#define UW_COMPONENTS_TERMINAL_SHELL_HOST_H_

#include <stddef.h>

#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "base/environment.h"
#include "base/files/file_path.h"
#include "base/files/scoped_file.h"
#include "base/process/process.h"

namespace uw {

// One shell on a pseudo-terminal. Destroying it ends the shell and whatever
// the shell was running.
class ShellHost {
 public:
  struct Options {
    base::FilePath shell;
    // A login shell reads the user's profile.
    bool login = true;
    base::FilePath folder;
    int columns = 80;
    int rows = 24;
    // Set on top of this process's environment.
    base::EnvironmentMap environment;
  };

  // The user's login shell in their home folder, with a UTF-8 locale when
  // this process has none. uw started from the Finder has none.
  static Options ForUser();

  // Null when the terminal or the shell could not be started.
  static std::unique_ptr<ShellHost> Start(const Options& options);

  ShellHost(const ShellHost&) = delete;
  ShellHost& operator=(const ShellHost&) = delete;
  ~ShellHost();

  // Readable when the shell has written output, and writable when it can
  // take more input. Reads and writes never block.
  int terminal_fd() const { return terminal_.get(); }

  // Sends `bytes` to the shell as if typed and returns how many the terminal
  // took. It takes fewer when the shell is not reading fast enough.
  size_t Write(std::string_view bytes);

  bool Resize(int columns, int rows);

  // The command the shell is running, or nothing while it waits at its prompt.
  std::optional<std::string> RunningCommand() const;

  // What the terminal waits for, read from its settings.
  struct Activity {
    // The shell's line editor waits for a command.
    bool at_prompt = false;
    // A line is read without being shown, as a password prompt reads one.
    bool password = false;
    // The program that has the terminal. Empty while the shell has it.
    std::string program;
  };
  Activity CurrentActivity() const;

  // The folder the shell is in. Empty when it cannot be read.
  base::FilePath Folder() const;

  base::ProcessId pid() const { return shell_.Pid(); }

 private:
  ShellHost(base::ScopedFD terminal, base::Process shell);

  base::ScopedFD terminal_;
  base::Process shell_;
};

}  // namespace uw

#endif  // UW_COMPONENTS_TERMINAL_SHELL_HOST_H_
