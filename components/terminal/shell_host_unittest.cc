// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/components/terminal/shell_host.h"

#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <unistd.h>

#include <array>
#include <memory>
#include <string>
#include <string_view>

#include "base/containers/span.h"
#include "base/environment.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/posix/eintr_wrapper.h"
#include "base/process/process_handle.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_view_util.h"
#include "base/test/task_environment.h"
#include "base/test/test_timeouts.h"
#include "base/threading/platform_thread.h"
#include "base/time/time.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace uw {
namespace {

constexpr std::string_view kPrompt = "ready> ";

class ShellHostTest : public testing::Test {
 protected:
  std::unique_ptr<ShellHost> StartShell(int columns = 80, int rows = 24) {
    ShellHost::Options options;
    options.shell = base::FilePath("/bin/sh");
    options.login = false;
    options.folder = base::FilePath("/usr");
    options.columns = columns;
    options.rows = rows;
    options.environment["PS1"] = std::string(kPrompt);
    std::unique_ptr<ShellHost> shell = ShellHost::Start(options);
    if (shell) {
      ReadUntil(*shell, kPrompt);
    }
    return shell;
  }

  // Everything the shell wrote, up to and including `marker`.
  std::string ReadUntil(const ShellHost& shell, std::string_view marker) {
    std::string output;
    const base::TimeTicks deadline =
        base::TimeTicks::Now() + TestTimeouts::action_timeout();
    while (output.find(marker) == std::string::npos &&
           base::TimeTicks::Now() < deadline) {
      pollfd waiting = {shell.terminal_fd(), POLLIN, 0};
      if (HANDLE_EINTR(poll(&waiting, 1, 50)) <= 0) {
        continue;
      }
      std::array<char, 4096> buffer;
      const ssize_t count =
          HANDLE_EINTR(read(shell.terminal_fd(), buffer.data(), buffer.size()));
      if (count <= 0) {
        break;
      }
      output.append(base::as_string_view(
          base::span(buffer).first(static_cast<size_t>(count))));
    }
    return output;
  }

  // Sends a command and returns what the shell wrote until it finished. The
  // shell computes the end marker, so the echo of the typed line never
  // matches it.
  std::string Run(ShellHost& shell, std::string_view command) {
    Type(shell, command);
    Type(shell, "; echo end-$((40+2))\n");
    return ReadUntil(shell, "end-42\r\n");
  }

  void Type(ShellHost& shell, std::string_view keys) {
    EXPECT_EQ(shell.Write(keys), keys.size());
  }

  bool WaitUntilGone(base::ProcessId pid) {
    const base::TimeTicks deadline =
        base::TimeTicks::Now() + TestTimeouts::action_timeout();
    while (base::TimeTicks::Now() < deadline) {
      if (kill(pid, 0) != 0 && errno == ESRCH) {
        return true;
      }
      base::PlatformThread::Sleep(base::Milliseconds(20));
    }
    return false;
  }

  // zsh, as most users run it, with a plain prompt.
  std::unique_ptr<ShellHost> StartZsh() {
    if (!zdotdir_.IsValid() &&
        (!zdotdir_.CreateUniqueTempDir() ||
         !base::WriteFile(zdotdir_.GetPath().Append(".zshrc"),
                          "PROMPT='ready> '\n"))) {
      return nullptr;
    }
    ShellHost::Options options;
    options.shell = base::FilePath("/bin/zsh");
    options.login = false;
    options.folder = base::FilePath("/usr");
    options.environment["ZDOTDIR"] = zdotdir_.GetPath().value();
    std::unique_ptr<ShellHost> shell = ShellHost::Start(options);
    if (shell) {
      ReadUntil(*shell, kPrompt);
    }
    return shell;
  }

  // Waits until the shell's activity reads as `wanted`, reading its output
  // meanwhile so that it never stops to wait for a reader.
  bool WaitForActivity(const ShellHost& shell,
                       bool (*wanted)(const ShellHost::Activity&)) {
    const base::TimeTicks deadline =
        base::TimeTicks::Now() + TestTimeouts::action_timeout();
    while (base::TimeTicks::Now() < deadline) {
      if (wanted(shell.CurrentActivity())) {
        return true;
      }
      pollfd waiting = {shell.terminal_fd(), POLLIN, 0};
      if (HANDLE_EINTR(poll(&waiting, 1, 20)) > 0) {
        std::array<char, 4096> buffer;
        HANDLE_EINTR(read(shell.terminal_fd(), buffer.data(), buffer.size()));
      }
    }
    return false;
  }

  bool WaitUntilRunning(const ShellHost& shell, std::string_view command) {
    const base::TimeTicks deadline =
        base::TimeTicks::Now() + TestTimeouts::action_timeout();
    while (base::TimeTicks::Now() < deadline) {
      if (shell.RunningCommand() == command) {
        return true;
      }
      base::PlatformThread::Sleep(base::Milliseconds(20));
    }
    return false;
  }

  base::test::TaskEnvironment task_environment_;
  base::ScopedTempDir zdotdir_;
};

TEST_F(ShellHostTest, RunsACommandAndReturnsItsOutput) {
  std::unique_ptr<ShellHost> shell = StartShell();
  ASSERT_TRUE(shell);
  EXPECT_NE(Run(*shell, "echo hello-$((40+2))").find("hello-42"),
            std::string::npos);
}

TEST_F(ShellHostTest, StartsInTheFolderAndSizeAskedFor) {
  std::unique_ptr<ShellHost> shell = StartShell(100, 30);
  ASSERT_TRUE(shell);
  EXPECT_NE(Run(*shell, "pwd").find("/usr\r\n"), std::string::npos);
  EXPECT_NE(Run(*shell, "stty size").find("30 100\r\n"), std::string::npos);
}

TEST_F(ShellHostTest, TheTerminalControlsTheShell) {
  std::unique_ptr<ShellHost> shell = StartShell();
  ASSERT_TRUE(shell);
  const std::string output = Run(*shell, "ps -o tty= -p $$");
  EXPECT_NE(output.find("ttys"), std::string::npos) << output;
}

TEST_F(ShellHostTest, SeesAResize) {
  std::unique_ptr<ShellHost> shell = StartShell();
  ASSERT_TRUE(shell);
  ASSERT_TRUE(shell->Resize(120, 40));
  EXPECT_NE(Run(*shell, "stty size").find("40 120\r\n"), std::string::npos);
}

TEST_F(ShellHostTest, ReportsTheShellsFolder) {
  std::unique_ptr<ShellHost> shell = StartShell();
  ASSERT_TRUE(shell);
  EXPECT_EQ(shell->Folder(), base::FilePath("/usr"));
  Run(*shell, "cd /private/tmp");
  EXPECT_EQ(shell->Folder(), base::FilePath("/private/tmp"));
}

TEST_F(ShellHostTest, NamesARunningCommandAndNothingAtThePrompt) {
  std::unique_ptr<ShellHost> shell = StartShell();
  ASSERT_TRUE(shell);
  EXPECT_FALSE(shell->RunningCommand());
  Type(*shell, "sleep 300\n");
  EXPECT_TRUE(WaitUntilRunning(*shell, "sleep"));
}

TEST_F(ShellHostTest, TellsThePromptACommandAndAPasswordPromptApart) {
  std::unique_ptr<ShellHost> shell = StartZsh();
  ASSERT_TRUE(shell);
  const auto at_prompt = [](const ShellHost::Activity& activity) {
    return activity.at_prompt && !activity.password && activity.program.empty();
  };
  EXPECT_TRUE(WaitForActivity(*shell, at_prompt));

  Type(*shell, "sleep 300\n");
  EXPECT_TRUE(WaitForActivity(*shell, [](const ShellHost::Activity& activity) {
    return !activity.at_prompt && !activity.password &&
           !activity.program_reads_keys && activity.program == "sleep";
  }));
  Type(*shell, "\x03");
  EXPECT_TRUE(WaitForActivity(*shell, at_prompt));

  // zsh's own read, so no other program has the terminal.
  Type(*shell, "read -s secret\n");
  EXPECT_TRUE(WaitForActivity(*shell, [](const ShellHost::Activity& activity) {
    return !activity.at_prompt && activity.password &&
           !activity.program_reads_keys;
  }));
  Type(*shell, "hunter2\n");
  EXPECT_TRUE(WaitForActivity(*shell, at_prompt));

  Type(*shell, "vi\n");
  EXPECT_TRUE(WaitForActivity(*shell, [](const ShellHost::Activity& activity) {
    return !activity.at_prompt && !activity.password &&
           activity.program_reads_keys && !activity.program.empty();
  }));
  Type(*shell, "\x1b:q!\n");
  EXPECT_TRUE(WaitForActivity(*shell, at_prompt));
}

// A program that reads keys one at a time, as ssh -t does, hides a prompt on
// the other machine, so its text tells.
TEST(ShellHostPromptTest, APasswordPromptReadsAsOne) {
  EXPECT_TRUE(ShellHost::IsPasswordPrompt("[sudo] password for admin: "));
  EXPECT_TRUE(ShellHost::IsPasswordPrompt("(admin@vm) Password:"));
  EXPECT_TRUE(ShellHost::IsPasswordPrompt(
      "Enter passphrase for key '/Users/a/.ssh/id_ed25519': "));
  EXPECT_TRUE(
      ShellHost::IsPasswordPrompt("Password for 'https://a@github.com':"));
  EXPECT_FALSE(ShellHost::IsPasswordPrompt("admin@vm ~ % "));
  EXPECT_FALSE(ShellHost::IsPasswordPrompt("Verification code: "));
  EXPECT_FALSE(ShellHost::IsPasswordPrompt("Your password was changed."));
  EXPECT_FALSE(ShellHost::IsPasswordPrompt(""));
}

TEST_F(ShellHostTest, CtrlCInterruptsARunningCommand) {
  std::unique_ptr<ShellHost> shell = StartShell();
  ASSERT_TRUE(shell);
  Type(*shell, "sleep 300\n");
  ASSERT_TRUE(WaitUntilRunning(*shell, "sleep"));
  Type(*shell, "\x03");
  Type(*shell, "echo status-$?-$((40+2))\n");
  EXPECT_NE(ReadUntil(*shell, "-42\r\n").find("status-130-42"),
            std::string::npos);
  EXPECT_FALSE(shell->RunningCommand());
}

TEST_F(ShellHostTest, ClosingEndsTheShellAndTheCommandItRuns) {
  std::unique_ptr<ShellHost> shell = StartShell();
  ASSERT_TRUE(shell);
  const std::string output = Run(*shell, "sleep 300 & echo child $! started");
  const size_t at = output.rfind("child ") + 6;
  int background = 0;
  ASSERT_TRUE(base::StringToInt(
      std::string_view(output).substr(at, output.find(' ', at) - at),
      &background))
      << output;
  Type(*shell, "sleep 300\n");
  ASSERT_TRUE(WaitUntilRunning(*shell, "sleep"));
  const base::ProcessId shell_pid = shell->pid();

  shell.reset();

  EXPECT_TRUE(WaitUntilGone(shell_pid));
  EXPECT_TRUE(WaitUntilGone(background));
}

TEST_F(ShellHostTest, TakesOnlyWhatTheTerminalHasRoomFor) {
  std::unique_ptr<ShellHost> shell = StartShell();
  ASSERT_TRUE(shell);
  Type(*shell, "sleep 300\n");
  ASSERT_TRUE(WaitUntilRunning(*shell, "sleep"));
  // Nothing reads these lines, so the terminal fills up.
  std::string paste;
  for (int line = 0; line < 1000; ++line) {
    paste += std::string(99, 'x') + "\n";
  }
  EXPECT_LT(shell->Write(paste), paste.size());
}

TEST_F(ShellHostTest, TheUsersShellExistsAndStartsAtHomeWithUtf8Text) {
  const ShellHost::Options options = ShellHost::ForUser();
  EXPECT_TRUE(base::PathExists(options.shell));
  EXPECT_TRUE(options.login);
  EXPECT_EQ(options.folder, base::GetHomeDir());
  EXPECT_EQ(options.environment.at("SHELL"), options.shell.value());
  const std::unique_ptr<base::Environment> environment =
      base::Environment::Create();
  if (!environment->HasVar("LANG") && !environment->HasVar("LC_ALL") &&
      !environment->HasVar("LC_CTYPE")) {
    EXPECT_TRUE(options.environment.at("LANG").ends_with(".UTF-8"));
  } else {
    EXPECT_FALSE(options.environment.contains("LANG"));
  }
}

}  // namespace
}  // namespace uw
