// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/components/uw_command/command_messages.h"

#include "base/json/json_reader.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace uw {
namespace {

base::DictValue Read(const std::string& line) {
  EXPECT_TRUE(line.ends_with("\n"));
  return base::JSONReader::ReadDict(line, base::JSON_PARSE_RFC).value();
}

TEST(CommandMessagesTest, ARequestHasAnIdAMethodAndParams) {
  std::optional<CommandRequest> request = ParseCommandRequest(
      R"({"jsonrpc":"2.0","id":3,"method":"settings.get",)"
      R"("params":{"name":"theme.grain"}})");
  ASSERT_TRUE(request);
  EXPECT_EQ(request->id, base::Value(3));
  EXPECT_EQ(request->method, "settings.get");
  EXPECT_EQ(*request->params.FindString("name"), "theme.grain");

  std::optional<CommandRequest> bare =
      ParseCommandRequest(R"({"id":"a","method":"hello"})");
  ASSERT_TRUE(bare);
  EXPECT_TRUE(bare->params.empty());
}

TEST(CommandMessagesTest, ALineThatIsNotARequestIsNothing) {
  EXPECT_FALSE(ParseCommandRequest("hello"));
  EXPECT_FALSE(ParseCommandRequest(R"({"id":1})"));
  EXPECT_FALSE(ParseCommandRequest(R"({"method":"hello"})"));
  EXPECT_FALSE(ParseCommandRequest(R"({"id":[1],"method":"hello"})"));
}

TEST(CommandMessagesTest, RepliesCarryTheRequestsId) {
  base::DictValue version;
  version.Set("version", "153.0.1");
  base::DictValue result =
      Read(CommandResult(base::Value(3), base::Value(std::move(version))));
  EXPECT_EQ(*result.FindString("jsonrpc"), "2.0");
  EXPECT_EQ(result.FindInt("id"), 3);
  EXPECT_EQ(*result.FindStringByDottedPath("result.version"), "153.0.1");

  base::DictValue error = Read(CommandError(
      base::Value("a"), kCommandUsage, "theme.grain takes 0 to 100."));
  EXPECT_EQ(*error.FindString("id"), "a");
  EXPECT_EQ(error.FindIntByDottedPath("error.code"), kCommandUsage);
  EXPECT_EQ(*error.FindStringByDottedPath("error.message"),
            "theme.grain takes 0 to 100.");
}

TEST(CommandMessagesTest, ANotificationHasAMethodAndNoId) {
  base::DictValue step;
  step.Set("kind", "tool");
  base::DictValue notification =
      Read(CommandNotification("chat.step", base::Value(std::move(step))));
  EXPECT_EQ(*notification.FindString("method"), "chat.step");
  EXPECT_FALSE(notification.contains("id"));
  EXPECT_EQ(*notification.FindStringByDottedPath("params.kind"), "tool");
}

}  // namespace
}  // namespace uw
