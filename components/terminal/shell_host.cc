// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/components/terminal/shell_host.h"

#include <fcntl.h>
#include <libproc.h>
#include <pwd.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>
#include <xlocale.h>

#include <algorithm>
#include <array>
#include <string_view>
#include <tuple>
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
#include "base/strings/string_util.h"

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
  const char* shell = options.shell.value().c_str();
  const char* folder =
      options.folder.empty() ? nullptr : options.folder.value().c_str();
  const int open_files = getdtablesize();
  // The child reports here when it cannot start the shell. A successful exec
  // closes it, so the parent reads nothing.
  std::array<int, 2> report;
  if (pipe(report.data()) != 0) {
    return -1;
  }
  base::ScopedFD failed(report[0]);
  base::ScopedFD failing(report[1]);
  if (!base::SetCloseOnExec(failed.get()) ||
      !base::SetCloseOnExec(failing.get())) {
    return -1;
  }

  // posix_spawn opens files before it starts the new session, so the shell
  // would have no controlling terminal: no job control, no Ctrl-C, and no
  // /dev/tty for sudo's password. The child takes the terminal as forkpty
  // does. Until exec, it makes only async-signal-safe calls.
  const pid_t pid = fork();
  if (pid < 0) {
    return -1;
  }
  if (pid > 0) {
    failing.reset();
    char failure;
    if (HANDLE_EINTR(read(failed.get(), &failure, 1)) != 0) {
      HANDLE_EINTR(waitpid(pid, nullptr, 0));
      return -1;
    }
    return pid;
  }
  setsid();
  const int terminal = open(terminal_name, O_RDWR);
  if (terminal < 0 || ioctl(terminal, TIOCSCTTY, nullptr) != 0) {
    std::ignore = write(failing.get(), "x", 1);
    _exit(127);
  }
  dup2(terminal, STDIN_FILENO);
  dup2(terminal, STDOUT_FILENO);
  dup2(terminal, STDERR_FILENO);
  for (int file = STDERR_FILENO + 1; file < open_files; ++file) {
    if (file != failing.get()) {
      close(file);
    }
  }
  if (folder) {
    std::ignore = chdir(folder);
  }
  struct sigaction default_action = {};
  default_action.sa_handler = SIG_DFL;
  for (int number = 1; number < NSIG; ++number) {
    sigaction(number, &default_action, nullptr);
  }
  sigset_t no_signals;
  sigemptyset(&no_signals);
  sigprocmask(SIG_SETMASK, &no_signals, nullptr);
  execve(shell, arguments.data(), environment.data());
  std::ignore = write(failing.get(), "x", 1);
  _exit(127);
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

ShellHost::Activity ShellHost::CurrentActivity() const {
  Activity activity;
  termios settings;
  if (tcgetattr(terminal_.get(), &settings) != 0) {
    return activity;
  }
  // A line editor, such as zsh's, reads keys one at a time, and so does a
  // full-screen program such as vim. A password prompt reads a whole line
  // with echo off.
  const bool lines = settings.c_lflag & ICANON;
  const bool echo = settings.c_lflag & ECHO;
  const pid_t foreground = tcgetpgrp(terminal_.get());
  activity.at_prompt = foreground == shell_.Pid() && !lines;
  activity.password = lines && !echo;
  activity.program_reads_keys =
      foreground > 0 && foreground != shell_.Pid() && !lines;
  activity.program = RunningCommand().value_or(std::string());
  return activity;
}

// static
bool ShellHost::IsPasswordPrompt(std::string_view line) {
  const std::string_view asked =
      base::TrimWhitespaceASCII(line, base::TRIM_TRAILING);
  if (!asked.ends_with(':')) {
    return false;
  }
  const std::string lower = base::ToLowerASCII(asked);
  return lower.find("password") != std::string::npos ||
         lower.find("passphrase") != std::string::npos;
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
