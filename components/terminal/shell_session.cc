// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/components/terminal/shell_session.h"

#include <errno.h>
#include <stdint.h>
#include <unistd.h>

#include <algorithm>
#include <memory>
#include <string_view>
#include <utility>

#include "base/containers/heap_array.h"
#include "base/containers/span.h"
#include "base/files/file_descriptor_watcher_posix.h"
#include "base/functional/bind.h"
#include "base/functional/callback.h"
#include "base/posix/eintr_wrapper.h"
#include "base/strings/string_util.h"
#include "base/strings/string_view_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/bind_post_task.h"
#include "base/task/thread_pool.h"
#include "base/time/time.h"
#include "base/timer/timer.h"

namespace uw {
namespace {

constexpr size_t kReadSize = 64 * 1024;
// The folder is read once output has stopped for this long. A prompt comes
// back after a `cd`, so the folder is read after every change.
constexpr base::TimeDelta kFolderDelay = base::Milliseconds(100);

// How many bytes at the end of `text` begin a UTF-8 character whose other
// bytes have not arrived yet.
size_t UnfinishedCharacter(std::string_view text) {
  for (size_t back = 1; back <= std::min<size_t>(text.size(), 3); ++back) {
    const uint8_t byte = static_cast<uint8_t>(text[text.size() - back]);
    if ((byte & 0xC0) == 0x80) {
      continue;
    }
    const size_t length = byte >= 0xF0   ? 4
                          : byte >= 0xE0 ? 3
                          : byte >= 0xC0 ? 2
                                         : 1;
    return length > back ? back : 0;
  }
  return 0;
}

}  // namespace

// Lives on the shell's own sequence.
class ShellSession::Core {
 public:
  using OutputCallback = base::RepeatingCallback<void(std::string)>;
  using FolderCallback = base::RepeatingCallback<void(const base::FilePath&)>;
  using ExitCallback = base::OnceCallback<void(bool)>;

  Core(base::OnceCallback<ShellHost::Options()> options,
       OutputCallback on_output,
       FolderCallback on_folder,
       ExitCallback on_exit)
      : on_output_(std::move(on_output)),
        on_folder_(std::move(on_folder)),
        on_exit_(std::move(on_exit)),
        shell_(ShellHost::Start(std::move(options).Run())) {
    if (!shell_) {
      std::move(on_exit_).Run(false);
      return;
    }
    WatchOutput();
  }

  void Write(std::string bytes) {
    if (!shell_) {
      return;
    }
    unwritten_.append(bytes);
    Flush();
  }

  void Resize(int columns, int rows) {
    if (shell_) {
      shell_->Resize(columns, rows);
    }
  }

  ShellHost::Activity CurrentActivity() const {
    return shell_ ? shell_->CurrentActivity() : ShellHost::Activity();
  }

  void WatchOutput() {
    if (shell_) {
      readable_ = base::FileDescriptorWatcher::WatchReadable(
          shell_->terminal_fd(),
          base::BindRepeating(&Core::Read, base::Unretained(this)));
    }
  }

 private:
  void Read() {
    base::span<char> buffer = buffer_.as_span();
    const ssize_t count =
        HANDLE_EINTR(read(shell_->terminal_fd(), buffer.data(), buffer.size()));
    if (count < 0 && errno == EAGAIN) {
      return;
    }
    if (count <= 0) {
      Exit();
      return;
    }
    unsent_.append(
        base::as_string_view(buffer.first(static_cast<size_t>(count))));
    const size_t whole = unsent_.size() - UnfinishedCharacter(unsent_);
    if (whole == 0) {
      return;
    }
    std::string text = unsent_.substr(0, whole);
    unsent_.erase(0, whole);
    if (!base::IsStringUTF8AllowingNoncharacters(text)) {
      // Bytes that are not text become the replacement character.
      text = base::UTF16ToUTF8(base::UTF8ToUTF16(text));
    }
    readable_.reset();
    on_output_.Run(std::move(text));
    folder_timer_.Start(FROM_HERE, kFolderDelay, this, &Core::ReadFolder);
  }

  void ReadFolder() {
    base::FilePath folder = shell_->Folder();
    if (folder.empty() || folder == folder_) {
      return;
    }
    folder_ = std::move(folder);
    on_folder_.Run(folder_);
  }

  void Flush() {
    unwritten_.erase(0, shell_->Write(unwritten_));
    if (unwritten_.empty()) {
      writable_.reset();
    } else if (!writable_) {
      writable_ = base::FileDescriptorWatcher::WatchWritable(
          shell_->terminal_fd(),
          base::BindRepeating(&Core::Flush, base::Unretained(this)));
    }
  }

  void Exit() {
    readable_.reset();
    writable_.reset();
    folder_timer_.Stop();
    shell_.reset();
    std::move(on_exit_).Run(true);
  }

  const OutputCallback on_output_;
  const FolderCallback on_folder_;
  ExitCallback on_exit_;
  std::unique_ptr<ShellHost> shell_;
  std::unique_ptr<base::FileDescriptorWatcher::Controller> readable_;
  std::unique_ptr<base::FileDescriptorWatcher::Controller> writable_;
  base::HeapArray<char> buffer_ = base::HeapArray<char>::Uninit(kReadSize);
  // Output that ends inside a character, kept until the rest arrives.
  std::string unsent_;
  // Input the terminal has not taken yet.
  std::string unwritten_;
  base::FilePath folder_;
  base::OneShotTimer folder_timer_;
};

ShellSession::ShellSession(base::OnceCallback<ShellHost::Options()> options,
                           Client* client)
    : client_(client) {
  // One sequence for each shell: ending a shell can take two seconds, and
  // that must not hold up another terminal.
  core_ = base::SequenceBound<Core>(
      base::ThreadPool::CreateSequencedTaskRunner(
          {base::MayBlock(), base::TaskPriority::USER_BLOCKING,
           base::TaskShutdownBehavior::SKIP_ON_SHUTDOWN}),
      std::move(options),
      base::BindPostTaskToCurrentDefault(base::BindRepeating(
          &ShellSession::OnOutput, weak_factory_.GetWeakPtr())),
      base::BindPostTaskToCurrentDefault(base::BindRepeating(
          &ShellSession::OnFolderChanged, weak_factory_.GetWeakPtr())),
      base::BindPostTaskToCurrentDefault(
          base::BindOnce(&ShellSession::OnExited, weak_factory_.GetWeakPtr())));
}

ShellSession::~ShellSession() = default;

void ShellSession::Write(std::string bytes) {
  core_.AsyncCall(&Core::Write).WithArgs(std::move(bytes));
}

void ShellSession::Resize(int columns, int rows) {
  core_.AsyncCall(&Core::Resize).WithArgs(columns, rows);
}

void ShellSession::AckOutput() {
  core_.AsyncCall(&Core::WatchOutput);
}

void ShellSession::GetActivity(
    base::OnceCallback<void(ShellHost::Activity)> done) {
  core_.AsyncCall(&Core::CurrentActivity).Then(std::move(done));
}

void ShellSession::OnOutput(std::string text) {
  client_->OnShellOutput(std::move(text));
}

void ShellSession::OnFolderChanged(const base::FilePath& folder) {
  client_->OnShellFolderChanged(folder);
}

void ShellSession::OnExited(bool started) {
  client_->OnShellExited(started);
}

}  // namespace uw
