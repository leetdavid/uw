// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/components/terminal/shell_quote.h"

#include "base/files/file_path.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace uw {
namespace {

TEST(ShellQuoteTest, EscapesWhatTheShellWouldRead) {
  EXPECT_EQ(
      QuotePathsForShell({base::FilePath("/Users/me/Screen Shots/a (1).png"),
                          base::FilePath("/tmp/it's $HOME&[x]")}),
      "/Users/me/Screen\\ Shots/a\\ \\(1\\).png "
      "/tmp/it\\'s\\ \\$HOME\\&\\[x\\]");
}

TEST(ShellQuoteTest, LeavesPlainPathsAlone) {
  EXPECT_EQ(QuotePathsForShell({base::FilePath("/tmp/notes.txt")}),
            "/tmp/notes.txt");
  EXPECT_EQ(QuotePathsForShell({}), "");
}

}  // namespace
}  // namespace uw
