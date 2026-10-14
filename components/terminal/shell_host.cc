// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/components/terminal/shell_host.h"

#include <fcntl.h>
#include <libproc.h>
#include <pwd.h>
#include <signal.h>
#include <spawn.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>
#include <xlocale.h>

#include <algorithm>
#include <array>
#include <string_view>
#include <utility>
#include <vector>

#include "base/compiler_specific.h"
#include "base/containers/heap_array.h"
#include "base/files/file_util.h"
#include "base/i18n/rtl.h"
#include "base/memory/ptr_util.h"
#include "base/posix/eintr_wrapper.h"
#include "base/process/environment_internal.h"
#include "base/process/kill.h"

namespace uw {
namespace {

winsize SizeOf(int columns, int rows) {
  return {static_cast<unsigned short>(rows),
          static_cast<unsigned short>(columns), 0, 0};
}

// The terminal's size and its UTF-8 flag are set through the shell's end. A
// size set on the other end before the shell opens the terminal is lost, and
// so are both once the shell's end has no open descriptor. The caller keeps
// the returned one open until the shell has started.
base::ScopedFD Prepare(const char* name, int columns, int rows) {
  base::ScopedFD shell_end(
      HANDLE_EINTR(open(name, O_RDWR | O_NOCTTY | O_CLOEXEC)));
  if (!shell_end.is_valid()) {
    return base::ScopedFD();
  }
  const winsize size = SizeOf(columns, rows);
  termios settings;
  if (ioctl(shell_end.get(), TIOCSWINSZ, &size) != 0 ||
      tcgetattr(shell_end.get(), &settings) != 0) {
    return base::ScopedFD();
  }
  settings.c_iflag |= IUTF8;
  if (tcsetattr(shell_end.get(), TCSANOW, &settings) != 0) {
    return base::ScopedFD();
  }
  return shell_end;
}

pid_t Spawn(const ShellHost::Options& options, const char* terminal_name) {
  posix_spawn_file_actions_t actions;
  if (posix_spawn_file_actions_init(&actions) != 0) {
    return -1;
  }
  // The shell opens the terminal itself after it becomes a session leader,
  // which makes the terminal its controlling terminal. Job control and Ctrl-C
  // depend on that.
  posix_spawn_file_actions_addopen(&actions, STDIN_FILENO, terminal_name,
                                   O_RDWR, 0);
  posix_spawn_file_actions_adddup2(&actions, STDIN_FILENO, STDOUT_FILENO);
  posix_spawn_file_actions_adddup2(&actions, STDIN_FILENO, STDERR_FILENO);
  if (!options.folder.empty()) {
    if (__builtin_available(macOS 26, *)) {
      posix_spawn_file_actions_addchdir(&actions,
                                        options.folder.value().c_str());
    } else {
      posix_spawn_file_actions_addchdir_np(&actions,
                                           options.folder.value().c_str());
    }
  }

  posix_spawnattr_t attributes;
  if (posix_spawnattr_init(&attributes) != 0) {
    posix_spawn_file_actions_destroy(&actions);
    return -1;
  }
  sigset_t no_signals;
  sigset_t all_signals;
  sigemptyset(&no_signals);
  sigfillset(&all_signals);
  posix_spawnattr_setsigmask(&attributes, &no_signals);
  posix_spawnattr_setsigdefault(&attributes, &all_signals);
  posix_spawnattr_setflags(&attributes,
                           POSIX_SPAWN_SETSID | POSIX_SPAWN_CLOEXEC_DEFAULT |
                               POSIX_SPAWN_SETSIGMASK | POSIX_SPAWN_SETSIGDEF);

  base::EnvironmentMap changes = options.environment;
  changes.try_emplace("TERM", "xterm-256color");
  changes.try_emplace("COLORTERM", "truecolor");
  changes.try_emplace("TERM_PROGRAM", "uw");
  // SAFETY: internal::GetEnvironment() promises that each string it returns
  // is null-terminated, which is what AlterEnvironment() requires.
  base::HeapArray<char*> environment =
      UNSAFE_BUFFERS(base::internal::AlterEnvironment(
          base::internal::GetEnvironment(), changes));

  std::string name = options.shell.BaseName().value();
  if (options.login) {
    name.insert(0, "-");
  }
  std::array<char*, 2> arguments = {name.data(), nullptr};

  pid_t pid = -1;
  const int result =
      posix_spawn(&pid, options.shell.value().c_str(), &actions, &attributes,
                  arguments.data(), environment.data());
  posix_spawn_file_actions_destroy(&actions);
  posix_spawnattr_destroy(&attributes);
  return result == 0 ? pid : -1;
}

// The user's language and region as a locale the C library knows, with UTF-8
// text. Without one, programs in the shell treat every character outside
// ASCII as unprintable.
std::string Utf8Locale() {
  std::string name = base::i18n::GetConfiguredLocale();
  std::ranges::replace(name, '-', '_');
  name += ".UTF-8";
  const locale_t known = newlocale(LC_ALL_MASK, name.c_str(), nullptr);
  if (!known) {
    return "en_US.UTF-8";
  }
  freelocale(known);
  return name;
}

}  // namespace

// static
ShellHost::Options ShellHost::ForUser() {
  Options options;
  options.shell = base::FilePath("/bin/zsh");
  options.folder = base::GetHomeDir();
  passwd user;
  passwd* found = nullptr;
  std::array<char, 4096> text;
  if (getpwuid_r(getuid(), &user, text.data(), text.size(), &found) == 0 &&
      found && found->pw_shell && !std::string_view(found->pw_shell).empty()) {
    options.shell = base::FilePath(found->pw_shell);
  }
  options.environment["SHELL"] = options.shell.value();
  const std::unique_ptr<base::Environment> environment =
      base::Environment::Create();
  if (!environment->HasVar("LC_ALL") && !environment->HasVar("LC_CTYPE") &&
      !environment->HasVar("LANG")) {
    options.environment["LANG"] = Utf8Locale();
  }
  return options;
}

// static
std::unique_ptr<ShellHost> ShellHost::Start(const Options& options) {
  base::ScopedFD terminal(HANDLE_EINTR(posix_openpt(O_RDWR | O_NOCTTY)));
  if (!terminal.is_valid() || grantpt(terminal.get()) != 0 ||
      unlockpt(terminal.get()) != 0) {
    return nullptr;
  }
  std::array<char, 128> name = {};
  if (ioctl(terminal.get(), TIOCPTYGNAME, name.data()) != 0 ||
      !base::SetCloseOnExec(terminal.get()) ||
      !base::SetNonBlocking(terminal.get())) {
    return nullptr;
  }
  const base::ScopedFD shell_end =
      Prepare(name.data(), options.columns, options.rows);
  if (!shell_end.is_valid()) {
    return nullptr;
  }
  const pid_t pid = Spawn(options, name.data());
  if (pid <= 0) {
    return nullptr;
  }
  return base::WrapUnique(
      new ShellHost(std::move(terminal), base::Process(pid)));
}

ShellHost::ShellHost(base::ScopedFD terminal, base::Process shell)
    : terminal_(std::move(terminal)), shell_(std::move(shell)) {}

ShellHost::~ShellHost() {
  // Closing the terminal hangs up on the shell and on the command it runs.
  terminal_.reset();
  base::EnsureProcessTerminated(std::move(shell_));
}

size_t ShellHost::Write(std::string_view bytes) {
  size_t written = 0;
  while (written < bytes.size()) {
    const std::string_view rest = bytes.substr(written);
    const ssize_t count =
        HANDLE_EINTR(write(terminal_.get(), rest.data(), rest.size()));
    if (count <= 0) {
      break;
    }
    written += static_cast<size_t>(count);
  }
  return written;
}

bool ShellHost::Resize(int columns, int rows) {
  const winsize size = SizeOf(columns, rows);
  return ioctl(terminal_.get(), TIOCSWINSZ, &size) == 0;
}

std::optional<std::string> ShellHost::RunningCommand() const {
  const pid_t foreground = tcgetpgrp(terminal_.get());
  if (foreground <= 0 || foreground == shell_.Pid()) {
    return std::nullopt;
  }
  std::array<char, 2 * MAXCOMLEN + 1> name = {};
  if (proc_name(foreground, name.data(), name.size()) <= 0) {
    return std::nullopt;
  }
  return std::string(name.data());
}

base::FilePath ShellHost::Folder() const {
  proc_vnodepathinfo info;
  if (proc_pidinfo(shell_.Pid(), PROC_PIDVNODEPATHINFO, 0, &info,
                   sizeof(info)) <= 0) {
    return base::FilePath();
  }
  return base::FilePath(std::string(std::data(info.pvi_cdir.vip_path)));
}

}  // namespace uw
