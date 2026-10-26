// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/components/terminal/shell_session.h"

#include <errno.h>
#include <signal.h>

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "base/files/file_path.h"
#include "base/functional/bind.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/test/run_until.h"
#include "base/test/task_environment.h"
#include "base/threading/platform_thread.h"
#include "base/time/time.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace uw {
namespace {

constexpr std::string_view kPrompt = "ready> ";

class ShellSessionTest : public testing::Test, public ShellSession::Client {
 protected:
  void StartShell(std::string_view shell = "/bin/sh") {
    ShellHost::Options options;
    options.shell = base::FilePath(shell);
    options.login = false;
    options.folder = base::FilePath("/usr");
    options.environment["PS1"] = std::string(kPrompt);
    session_ = std::make_unique<ShellSession>(
        base::BindOnce([](ShellHost::Options options) { return options; },
                       options),
        this);
  }

  // ShellSession::Client:
  void OnShellOutput(std::string text) override {
    chunks_.push_back(text);
    output_ += text;
    if (acks_) {
      session_->AckOutput();
    }
  }
  void OnShellFolderChanged(const base::FilePath& folder) override {
    folder_ = folder;
  }
  void OnShellExited(bool started) override {
    exited_ = true;
    started_ = started;
  }

  [[nodiscard]] bool WaitFor(std::string_view marker) {
    return base::test::RunUntil(
        [&] { return output_.find(marker) != std::string::npos; });
  }

  // Sends a command and waits until the shell has finished it. The shell
  // computes the end marker, so the echo of the typed line never matches it.
  [[nodiscard]] bool Run(std::string_view command) {
    output_.clear();
    session_->Write(std::string(command) + "; echo end-$((40+2))\n");
    return WaitFor("end-42\r\n");
  }

  base::test::TaskEnvironment task_environment_;
  std::unique_ptr<ShellSession> session_;
  std::vector<std::string> chunks_;
  std::string output_;
  base::FilePath folder_;
  bool acks_ = true;
  bool exited_ = false;
  bool started_ = false;
};

TEST_F(ShellSessionTest, RunsACommandAndDeliversItsOutput) {
  StartShell();
  ASSERT_TRUE(WaitFor(kPrompt));
  ASSERT_TRUE(Run("echo hello-$((40+2))"));
  EXPECT_NE(output_.find("hello-42"), std::string::npos);
}

TEST_F(ShellSessionTest, HoldsOutputBackUntilTheLastWasAcknowledged) {
  acks_ = false;
  StartShell();
  ASSERT_TRUE(WaitFor(kPrompt));
  const size_t delivered = chunks_.size();

  session_->Write("echo held-$((40+2))\n");
  base::PlatformThread::Sleep(base::Milliseconds(300));
  task_environment_.RunUntilIdle();
  EXPECT_EQ(chunks_.size(), delivered);

  acks_ = true;
  session_->AckOutput();
  EXPECT_TRUE(WaitFor("held-42"));
}

TEST_F(ShellSessionTest, DeliversOnlyWholeCharacters) {
  StartShell();
  ASSERT_TRUE(WaitFor(kPrompt));
  // The euro sign is three bytes. The first arrives well before the others.
  ASSERT_TRUE(Run("printf '\\342'; sleep 0.3; printf '\\202\\254'"));
  EXPECT_NE(output_.find("\xE2\x82\xAC"), std::string::npos);
  for (const std::string& chunk : chunks_) {
    EXPECT_TRUE(base::IsStringUTF8(chunk)) << chunk;
  }
}

TEST_F(ShellSessionTest, ReplacesBytesThatAreNotText) {
  StartShell();
  ASSERT_TRUE(WaitFor(kPrompt));
  ASSERT_TRUE(Run("printf 'a\\377b'"));
  EXPECT_NE(output_.find("a\xEF\xBF\xBD"
                         "b"),
            std::string::npos);
}

TEST_F(ShellSessionTest, PassesOnAPasteLargerThanTheTerminalTakesAtOnce) {
  StartShell();
  ASSERT_TRUE(WaitFor(kPrompt));
  ASSERT_TRUE(Run("stty -echo"));
  output_.clear();
  session_->Write("wc -l\n");
  std::string paste;
  for (int line = 0; line < 4000; ++line) {
    paste += std::string(99, 'x') + "\n";
  }
  session_->Write(paste);
  session_->Write("\x04");
  EXPECT_TRUE(WaitFor("4000\r\n"));
}

TEST_F(ShellSessionTest, ReportsTheFolderAndEachChangeOfIt) {
  StartShell();
  EXPECT_TRUE(
      base::test::RunUntil([&] { return folder_ == base::FilePath("/usr"); }));
  session_->Write("cd /private/tmp\n");
  EXPECT_TRUE(base::test::RunUntil(
      [&] { return folder_ == base::FilePath("/private/tmp"); }));
}

TEST_F(ShellSessionTest, ReportsThatTheShellExited) {
  StartShell();
  ASSERT_TRUE(WaitFor(kPrompt));
  session_->Write("exit\n");
  EXPECT_TRUE(base::test::RunUntil([&] { return exited_; }));
  EXPECT_TRUE(started_);
}

TEST_F(ShellSessionTest, ReportsAnExitWhileACommandStillHoldsTheTerminal) {
  StartShell();
  ASSERT_TRUE(WaitFor(kPrompt));
  // The shell's exit hangs up on the command it leaves behind.
  session_->Write("sleep 300 &\n");
  session_->Write("exit\n");
  EXPECT_TRUE(base::test::RunUntil([&] { return exited_; }));
}

TEST_F(ShellSessionTest, ReportsAShellThatCannotStart) {
  StartShell("/nonexistent/shell");
  EXPECT_TRUE(base::test::RunUntil([&] { return exited_; }));
  EXPECT_FALSE(started_);
}

TEST_F(ShellSessionTest, ClosingEndsTheShell) {
  StartShell();
  ASSERT_TRUE(WaitFor(kPrompt));
  ASSERT_TRUE(Run("echo pid $$ ok"));
  const size_t at = output_.rfind("pid ") + 4;
  int shell = 0;
  ASSERT_TRUE(base::StringToInt(
      std::string_view(output_).substr(at, output_.find(' ', at) - at), &shell))
      << output_;

  session_.reset();

  EXPECT_TRUE(base::test::RunUntil(
      [&] { return kill(shell, 0) != 0 && errno == ESRCH; }));
}

}  // namespace
}  // namespace uw
