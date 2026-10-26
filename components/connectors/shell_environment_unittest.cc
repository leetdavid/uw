// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/components/connectors/shell_environment.h"

#include "base/strings/strcat.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace uw {
namespace {

// What a shell prints for ShellEnvironmentCommand, with `before` and `after`
// from the user's startup files around it.
std::string Printed(std::string_view before,
                    std::string_view entries,
                    std::string_view after) {
  const std::string command = ShellEnvironmentCommand();
  const size_t open = command.find("'__");
  const std::string mark =
      command.substr(open + 1, command.find('\'', open + 1) - open - 1);
  return base::StrCat({before, mark, entries, mark, after});
}

TEST(ShellEnvironmentTest, ReadsTheVariablesBetweenTheMarks) {
  using std::string_literals::operator""s;
  std::optional<base::EnvironmentMap> environment = ParseShellEnvironment(
      Printed("Welcome back!\n",
              "PATH=/Users/a/.fnm/bin:/usr/bin\0GREETING=a=b\nc\0EMPTY=\0"s,
              "\x1b]0;title\x07"));
  ASSERT_TRUE(environment);
  EXPECT_EQ(environment->at("PATH"), "/Users/a/.fnm/bin:/usr/bin");
  EXPECT_EQ(environment->at("GREETING"), "a=b\nc");
  EXPECT_EQ(environment->at("EMPTY"), "");
}

TEST(ShellEnvironmentTest, NothingWithoutBothMarksOrAPath) {
  using std::string_literals::operator""s;
  EXPECT_FALSE(ParseShellEnvironment(""));
  EXPECT_FALSE(ParseShellEnvironment("PATH=/usr/bin"));
  EXPECT_FALSE(ParseShellEnvironment(Printed("", "HOME=/Users/a\0"s, "")));
}

}  // namespace
}  // namespace uw
