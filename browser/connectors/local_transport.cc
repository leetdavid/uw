// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/browser/connectors/local_transport.h"

#include <signal.h>
#include <unistd.h>

#include <array>
#include <utility>
#include <vector>

#include "base/files/file.h"
#include "base/files/file_descriptor_watcher_posix.h"
#include "base/files/file_util.h"
#include "base/files/scoped_file.h"
#include "base/functional/bind.h"
#include "base/posix/eintr_wrapper.h"
#include "base/process/kill.h"
#include "base/process/launch.h"
#include "base/process/process.h"
#include "base/strings/string_util.h"
#include "base/task/bind_post_task.h"
#include "base/task/thread_pool.h"

namespace uw {
namespace {

// A log longer than this starts over when the connector starts.
constexpr int64_t kMaxLog = 1024 * 1024;
// Longest line uw reads; a screenshot can be several megabytes.
constexpr size_t kMaxLine = 32 * 1024 * 1024;
// Longest error line the connectors page shows.
constexpr size_t kMaxErrorLine = 300;

}  // namespace

// The program and its pipes, on a sequence that may block.
class LocalTransport::Pipes {
 public:
  Pipes(const ConnectorConfig& connector,
        const base::FilePath& shell,
        const std::optional<base::EnvironmentMap>& environment,
        const base::FilePath& log,
        base::RepeatingCallback<void(std::string)> on_line,
        base::OnceCallback<void(std::string)> on_stopped)
      : on_line_(std::move(on_line)), on_stopped_(std::move(on_stopped)) {
    if (base::GetFileSize(log).value_or(0) > kMaxLog) {
      base::DeleteFile(log);
    }
    // The first connector added may start before its folder is made.
    base::CreateDirectory(log.DirName());
    log_ =
        base::File(log, base::File::FLAG_OPEN_ALWAYS | base::File::FLAG_APPEND);

    std::array<int, 2> input;
    std::array<int, 2> output;
    std::array<int, 2> errors;
    if (pipe(input.data()) != 0) {
      Stop("uw couldn't start it.");
      return;
    }
    base::ScopedFD input_read(input[0]);
    input_.reset(input[1]);
    if (pipe(output.data()) != 0) {
      Stop("uw couldn't start it.");
      return;
    }
    output_.reset(output[0]);
    base::ScopedFD output_write(output[1]);
    if (pipe(errors.data()) != 0) {
      Stop("uw couldn't start it.");
      return;
    }
    errors_.reset(errors[0]);
    base::ScopedFD errors_write(errors[1]);

    base::LaunchOptions options;
    options.fds_to_remap = {{input_read.get(), STDIN_FILENO},
                            {output_write.get(), STDOUT_FILENO},
                            {errors_write.get(), STDERR_FILENO}};
    options.current_directory = base::GetHomeDir();
    options.new_process_group = true;
    std::vector<std::string> argv;
    if (environment) {
      // The terminal's environment is complete; a login shell would read the
      // user's profile again and could put another PATH first.
      options.clear_environment = true;
      options.environment = *environment;
      argv = {"/bin/sh", "-c"};
    } else {
      argv = {shell.value(), "-l", "-c"};
    }
    options.environment["SHELL"] = shell.value();
    for (const auto& [key, value] : connector.env) {
      options.environment[key] = value;
    }
    argv.insert(argv.end(), {"exec \"$0\" \"$@\"", connector.command});
    argv.insert(argv.end(), connector.args.begin(), connector.args.end());
    process_ = base::LaunchProcess(argv, options);
    if (!process_.IsValid()) {
      Stop("uw couldn't start your login shell.");
      return;
    }
    base::SetNonBlocking(output_.get());
    base::SetNonBlocking(errors_.get());
    output_watch_ = base::FileDescriptorWatcher::WatchReadable(
        output_.get(),
        base::BindRepeating(&Pipes::OnOutput, base::Unretained(this)));
    errors_watch_ = base::FileDescriptorWatcher::WatchReadable(
        errors_.get(),
        base::BindRepeating(&Pipes::OnErrors, base::Unretained(this)));
  }

  Pipes(const Pipes&) = delete;
  Pipes& operator=(const Pipes&) = delete;

  ~Pipes() {
    output_watch_.reset();
    errors_watch_.reset();
    // A connector quits when its input ends. Those that don't are ended.
    input_.reset();
    if (!process_.IsValid()) {
      return;
    }
    const pid_t group = process_.Pid();
    if (!process_.WaitForExitWithTimeout(base::Seconds(1), nullptr)) {
      kill(-group, SIGTERM);
      base::EnsureProcessTerminated(std::move(process_));
    }
    kill(-group, SIGKILL);
  }

  void Write(std::string line) {
    if (!input_.is_valid()) {
      return;
    }
    line.push_back('\n');
    if (!base::WriteFileDescriptor(input_.get(), line)) {
      Stop("uw couldn't write to it.");
    }
  }

 private:
  void OnOutput() {
    std::array<char, 64 * 1024> buffer;
    const ssize_t got =
        HANDLE_EINTR(read(output_.get(), buffer.data(), buffer.size()));
    if (got < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
      return;
    }
    if (got <= 0) {
      Stop(std::string());
      return;
    }
    line_.append(buffer.data(), static_cast<size_t>(got));
    for (size_t end = line_.find('\n'); end != std::string::npos;
         end = line_.find('\n')) {
      std::string line = line_.substr(0, end);
      line_.erase(0, end + 1);
      if (!base::TrimWhitespaceASCII(line, base::TRIM_ALL).empty()) {
        on_line_.Run(std::move(line));
      }
    }
    if (line_.size() > kMaxLine) {
      Stop("It sent a message too large to read.");
    }
  }

  void OnErrors() { ReadErrors(); }

  // Reads what is waiting on the error output. False once it has ended or
  // nothing more is waiting.
  bool ReadErrors() {
    if (!errors_.is_valid()) {
      return false;
    }
    std::array<char, 16 * 1024> buffer;
    const ssize_t got =
        HANDLE_EINTR(read(errors_.get(), buffer.data(), buffer.size()));
    if (got < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
      return false;
    }
    if (got <= 0) {
      Keep(base::TrimWhitespaceASCII(error_line_, base::TRIM_ALL));
      error_line_.clear();
      errors_watch_.reset();
      errors_.reset();
      return false;
    }
    const std::string_view text(buffer.data(), static_cast<size_t>(got));
    if (log_.IsValid()) {
      log_.WriteAtCurrentPos(base::as_byte_span(text));
    }
    error_line_.append(text);
    for (size_t end = error_line_.find('\n'); end != std::string::npos;
         end = error_line_.find('\n')) {
      Keep(base::TrimWhitespaceASCII(
          std::string_view(error_line_).substr(0, end), base::TRIM_ALL));
      error_line_.erase(0, end + 1);
    }
    if (error_line_.size() > kMaxErrorLine) {
      error_line_.clear();
    }
    return true;
  }

  // Keeps `line` of the error output as the last one, and as the last that
  // names an error when it does, such as "ReferenceError: File is not
  // defined" before Node's closing "Node.js v18.6.0".
  void Keep(std::string_view line) {
    if (line.empty()) {
      return;
    }
    last_error_ = std::string(line.substr(0, kMaxErrorLine));
    if (line.find("rror:") != std::string_view::npos) {
      named_error_ = last_error_;
    }
  }

  // Reports the end once: `why`, or the last error it named on its error
  // output, or else the last thing it said there.
  void Stop(std::string why) {
    output_watch_.reset();
    if (!on_stopped_) {
      return;
    }
    // A program that quits often says why just before; let it finish.
    if (process_.IsValid() &&
        process_.WaitForExitWithTimeout(base::Milliseconds(500), nullptr)) {
      process_.Close();
    }
    while (ReadErrors()) {
    }
    if (why.empty()) {
      why = !named_error_.empty()  ? named_error_
            : !last_error_.empty() ? last_error_
                                   : "It stopped.";
    }
    std::move(on_stopped_).Run(std::move(why));
  }

  base::Process process_;
  base::ScopedFD input_;
  base::ScopedFD output_;
  base::ScopedFD errors_;
  base::File log_;
  std::unique_ptr<base::FileDescriptorWatcher::Controller> output_watch_;
  std::unique_ptr<base::FileDescriptorWatcher::Controller> errors_watch_;
  // What came after the last whole line.
  std::string line_;
  std::string error_line_;
  std::string last_error_;
  std::string named_error_;
  base::RepeatingCallback<void(std::string)> on_line_;
  base::OnceCallback<void(std::string)> on_stopped_;
};

LocalTransport::LocalTransport(
    const ConnectorConfig& connector,
    const base::FilePath& shell,
    const std::optional<base::EnvironmentMap>& environment,
    const base::FilePath& log,
    OnMessage on_notification,
    OnExit on_exit)
    : on_notification_(std::move(on_notification)),
      on_exit_(std::move(on_exit)) {
  pipes_.emplace(base::ThreadPool::CreateSequencedTaskRunner(
                     {base::MayBlock(), base::TaskPriority::USER_VISIBLE,
                      base::TaskShutdownBehavior::SKIP_ON_SHUTDOWN}),
                 connector, shell, environment, log,
                 base::BindPostTaskToCurrentDefault(base::BindRepeating(
                     &LocalTransport::OnLine, weak_factory_.GetWeakPtr())),
                 base::BindPostTaskToCurrentDefault(base::BindOnce(
                     &LocalTransport::OnStopped, weak_factory_.GetWeakPtr())));
}

LocalTransport::~LocalTransport() = default;

void LocalTransport::Send(int id,
                          std::string_view version,
                          mcp::Era era,
                          std::string_view method,
                          std::string_view name,
                          std::string body,
                          Done done) {
  if (stopped_) {
    std::move(done).Run(McpReply::Problem("The connector isn't running."));
    return;
  }
  pending_[id] = std::move(done);
  pipes_.AsyncCall(&Pipes::Write).WithArgs(std::move(body));
}

void LocalTransport::Notify(std::string body) {
  if (!stopped_) {
    pipes_.AsyncCall(&Pipes::Write).WithArgs(std::move(body));
  }
}

void LocalTransport::Forget(int id) {
  pending_.erase(id);
}

void LocalTransport::OnLine(std::string line) {
  std::optional<mcp::Message> message = mcp::ParseMessage(line);
  if (!message) {
    return;
  }
  if (!message->method.empty()) {
    if (!message->request_id.is_none()) {
      pipes_.AsyncCall(&Pipes::Write)
          .WithArgs(mcp::AnswerToServer(message->request_id, message->method));
      return;
    }
    on_notification_.Run(*message);
    return;
  }
  if (!message->id) {
    return;
  }
  auto pending = pending_.find(*message->id);
  if (pending == pending_.end()) {
    return;
  }
  Done done = std::move(pending->second);
  pending_.erase(pending);
  McpReply reply;
  reply.message = std::move(message);
  std::move(done).Run(std::move(reply));
}

void LocalTransport::OnStopped(std::string last_error) {
  stopped_ = true;
  base::WeakPtr<LocalTransport> alive = weak_factory_.GetWeakPtr();
  std::map<int, Done> pending = std::move(pending_);
  for (auto& [id, done] : pending) {
    std::move(done).Run(McpReply::Problem(last_error));
    if (!alive) {
      return;
    }
  }
  if (on_exit_) {
    std::move(on_exit_).Run(std::move(last_error));
  }
}

}  // namespace uw
