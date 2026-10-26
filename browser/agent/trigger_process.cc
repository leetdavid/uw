// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/browser/agent/trigger_process.h"

#include <fcntl.h>
#include <signal.h>
#include <unistd.h>

#include <array>
#include <optional>
#include <utility>
#include <vector>

#include "base/environment.h"
#include "base/files/file_descriptor_watcher_posix.h"
#include "base/files/file_util.h"
#include "base/files/scoped_file.h"
#include "base/functional/bind.h"
#include "base/posix/eintr_wrapper.h"
#include "base/process/launch.h"
#include "base/process/process.h"
#include "base/task/bind_post_task.h"
#include "base/task/thread_pool.h"
#include "base/timer/timer.h"
#include "uw/browser/connectors/connectors.h"
#include "uw/components/terminal/shell_host.h"

namespace uw {
namespace {

// How often uw looks whether the command has ended. Its output can stay open
// after it ends, held by a program it left running.
constexpr base::TimeDelta kExitCheck = base::Seconds(1);
// A line longer than this comes back in pieces.
constexpr size_t kMaxLine = 64 * 1024;

}  // namespace

// The command and its output, on a sequence that may block.
class TriggerProcess::Pipe {
 public:
  Pipe(const std::string& command,
       const base::FilePath& folder,
       const base::FilePath& shell,
       const std::optional<base::EnvironmentMap>& environment,
       OnLine on_line,
       OnEnd on_end)
      : on_line_(std::move(on_line)), on_end_(std::move(on_end)) {
    std::array<int, 2> output;
    base::ScopedFD nothing(HANDLE_EINTR(open("/dev/null", O_RDONLY)));
    if (!nothing.is_valid() || pipe(output.data()) != 0) {
      End(-1);
      return;
    }
    output_.reset(output[0]);
    base::ScopedFD output_write(output[1]);
    base::LaunchOptions options;
    options.fds_to_remap = {{nothing.get(), STDIN_FILENO},
                            {output_write.get(), STDOUT_FILENO},
                            {output_write.get(), STDERR_FILENO}};
    options.current_directory =
        base::DirectoryExists(folder) ? folder : base::GetHomeDir();
    options.new_process_group = true;
    std::vector<std::string> argv;
    if (environment) {
      // The terminal's environment is complete, so the shell need not read
      // the user's profile again.
      options.clear_environment = true;
      options.environment = *environment;
      argv = {shell.value(), "-c", command};
    } else {
      argv = {shell.value(), "-l", "-c", command};
    }
    options.environment["SHELL"] = shell.value();
    process_ = base::LaunchProcess(argv, options);
    if (!process_.IsValid()) {
      End(-1);
      return;
    }
    base::SetNonBlocking(output_.get());
    output_watch_ = base::FileDescriptorWatcher::WatchReadable(
        output_.get(),
        base::BindRepeating(&Pipe::OnReadable, base::Unretained(this)));
    exit_check_.Start(
        FROM_HERE, kExitCheck,
        base::BindRepeating(&Pipe::CheckExit, base::Unretained(this)));
  }

  Pipe(const Pipe&) = delete;
  Pipe& operator=(const Pipe&) = delete;

  ~Pipe() {
    output_watch_.reset();
    if (process_.IsValid()) {
      EndGroup();
    }
  }

 private:
  void OnReadable() { Read(); }

  // Reads what waits. False once nothing more waits or the output ended.
  bool Read() {
    if (!output_.is_valid()) {
      return false;
    }
    std::array<char, 16 * 1024> buffer;
    const ssize_t got =
        HANDLE_EINTR(read(output_.get(), buffer.data(), buffer.size()));
    if (got < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
      return false;
    }
    if (got <= 0) {
      output_watch_.reset();
      output_.reset();
      return false;
    }
    line_.append(buffer.data(), static_cast<size_t>(got));
    for (size_t end = line_.find('\n'); end != std::string::npos;
         end = line_.find('\n')) {
      on_line_.Run(line_.substr(0, end));
      line_.erase(0, end + 1);
    }
    if (line_.size() > kMaxLine) {
      on_line_.Run(std::exchange(line_, std::string()));
    }
    return true;
  }

  void CheckExit() {
    int code = 0;
    if (!process_.IsValid() ||
        !process_.WaitForExitWithTimeout(base::TimeDelta(), &code)) {
      return;
    }
    // What it printed last may still wait to be read.
    while (Read()) {
    }
    if (!line_.empty()) {
      on_line_.Run(std::exchange(line_, std::string()));
    }
    // A program it left running ends with it.
    EndGroup();
    End(code);
  }

  void EndGroup() {
    const pid_t group = process_.Pid();
    kill(-group, SIGTERM);
    if (!process_.WaitForExitWithTimeout(base::Milliseconds(200), nullptr)) {
      kill(-group, SIGKILL);
    }
    process_.Close();
  }

  void End(int code) {
    exit_check_.Stop();
    output_watch_.reset();
    if (on_end_) {
      std::move(on_end_).Run(code);
    }
  }

  base::Process process_;
  base::ScopedFD output_;
  std::unique_ptr<base::FileDescriptorWatcher::Controller> output_watch_;
  base::RepeatingTimer exit_check_;
  // What came after the last whole line.
  std::string line_;
  OnLine on_line_;
  OnEnd on_end_;
};

TriggerProcess::TriggerProcess(const std::string& command,
                               const base::FilePath& folder,
                               OnLine on_line,
                               OnEnd on_end) {
  // The command ends with uw, so the sequence that ends it must run at
  // shutdown.
  pipe_.emplace(base::ThreadPool::CreateSequencedTaskRunner(
                    {base::MayBlock(), base::WithBaseSyncPrimitives(),
                     base::TaskPriority::USER_VISIBLE,
                     base::TaskShutdownBehavior::BLOCK_SHUTDOWN}),
                command, folder, ShellHost::ForUser().shell,
                Connectors::Get().environment(),
                base::BindPostTaskToCurrentDefault(std::move(on_line)),
                base::BindPostTaskToCurrentDefault(std::move(on_end)));
}

TriggerProcess::~TriggerProcess() = default;

}  // namespace uw
