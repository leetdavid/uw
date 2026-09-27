// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/components/tab_organization/ai_protocol.h"

#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/strings/string_util.h"
#include "base/test/task_environment.h"
#include "base/values.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace uw {
namespace {

class AiProtocolTest : public testing::Test {
 protected:
  base::test::TaskEnvironment environment_;
};

// Wraps assistant `content` in a chat-completions response body.
std::string WrapContent(const std::string& content) {
  base::DictValue message;
  message.Set("role", "assistant");
  message.Set("content", content);
  base::DictValue choice;
  choice.Set("message", std::move(message));
  base::ListValue choices;
  choices.Append(std::move(choice));
  base::DictValue body;
  body.Set("choices", std::move(choices));
  return base::WriteJson(body).value();
}

TEST_F(AiProtocolTest, ClassificationRequestRoundTripsContext) {
  BranchContext context;
  context.title = "Benchmark Jev";
  context.url = "https://example.com/bench";
  context.source_description = "opened from tab A";
  context.excerpt = "A short readable excerpt.";
  context.manual_placement = "the user manually placed this branch in \"Other\"";
  auto body = BuildClassificationRequestBody(
      "test-model", context,
      {{"task-1", "Research"}, {kNewTaskOption, "x"}, {"", "y"}});
  // Reserved and empty option keys are rejected.
  EXPECT_FALSE(body);

  body = BuildClassificationRequestBody(
      "test-model", context, {{"task-1", "Research"}, {"task-2", "Travel"}});
  ASSERT_TRUE(body);
  auto parsed = base::JSONReader::ReadDict(*body, base::JSON_PARSE_RFC);
  ASSERT_TRUE(parsed);
  EXPECT_EQ(*parsed->FindString("model"), "test-model");
  const std::string* user_content =
      (*parsed->FindList("messages"))[1].GetDict().FindString("content");
  ASSERT_TRUE(user_content);
  auto description = base::JSONReader::ReadDict(*user_content);
  ASSERT_TRUE(description);
  EXPECT_EQ(*description->FindString("title"), "Benchmark Jev");
  EXPECT_EQ(*description->FindString("url"), "https://example.com/bench");
  EXPECT_EQ(*description->FindString("source"), "opened from tab A");
  EXPECT_EQ(*description->FindString("excerpt"), "A short readable excerpt.");
  EXPECT_EQ(*description->FindString("manual_placement"),
            "the user manually placed this branch in \"Other\"");
  EXPECT_EQ(*description->FindStringByDottedPath("tasks.0.key"), "task-1");
  EXPECT_EQ(*description->FindStringByDottedPath("tasks.1.name"), "Travel");
}

TEST_F(AiProtocolTest, ClassificationRequestRejectsEmptyInput) {
  EXPECT_FALSE(BuildClassificationRequestBody("", BranchContext(), {}));
  EXPECT_FALSE(BuildClassificationRequestBody("model", BranchContext(), {}));
}

TEST_F(AiProtocolTest, ClassificationParsesAndValidates) {
  auto answer = ParseClassificationResponse(WrapContent(R"({
    "decision": "move", "target": "task-1", "confidence": 0.93})")));
  ASSERT_TRUE(answer);
  EXPECT_EQ(answer->choice, "task-1");
  EXPECT_DOUBLE_EQ(answer->confidence, 0.93);
  EXPECT_FALSE(answer->task_name);

  answer = ParseClassificationResponse(WrapContent(
      R"({"decision": "new_task", "confidence": 0.95, "name": "  Benchmark Jev  "})")));
  ASSERT_TRUE(answer);
  EXPECT_EQ(answer->choice, kNewTaskOption);
  EXPECT_EQ(*answer->task_name, u"Benchmark Jev");

  answer = ParseClassificationResponse(
      WrapContent(R"({"decision": "leave", "confidence": 0.4})")));
  ASSERT_TRUE(answer);
  EXPECT_EQ(answer->choice, kScratchpadOption);
}

TEST_F(AiProtocolTest, ClassificationAcceptsCodeFencedContent) {
  auto answer = ParseClassificationResponse(
      WrapContent("```\n{\"decision\": \"leave\", \"confidence\": 1.0}\n```"));
  ASSERT_TRUE(answer);
  EXPECT_EQ(answer->choice, kScratchpadOption);
}

TEST_F(AiProtocolTest, NewTaskNameIsCappedAndOptional) {
  auto answer = ParseClassificationResponse(WrapContent(R"({
    "decision": "new_task", "confidence": 0.95, "name": "  "})")));
  ASSERT_TRUE(answer);
  EXPECT_FALSE(answer->task_name);

  const std::string long_name(200, 'a');
  answer = ParseClassificationResponse(WrapContent(
      "{\"decision\": \"new_task\", \"confidence\": 0.95, \"name\": \"" +
      long_name + "\"}"));
  ASSERT_TRUE(answer);
  ASSERT_TRUE(answer->task_name);
  EXPECT_EQ(answer->task_name->size(), kMaxGeneratedTaskNameLength);
}

TEST_F(AiProtocolTest, ClassificationRejectsMalformedResponses) {
  // Not JSON.
  EXPECT_FALSE(ParseClassificationResponse("not json"));
  // Empty choices.
  EXPECT_FALSE(ParseClassificationResponse(R"({"choices": []})"));
  // Missing content.
  EXPECT_FALSE(ParseClassificationResponse(R"({"choices": [{"message": {}}]})"));
  // Unknown decision.
  EXPECT_FALSE(ParseClassificationResponse(
      WrapContent(R"({"decision": "merge", "confidence": 1.0})")));
  // Move without a target.
  EXPECT_FALSE(
      ParseClassificationResponse(WrapContent(R"({"decision": "move"})")));
  // Move targeting a reserved option key.
  EXPECT_FALSE(ParseClassificationResponse(WrapContent(
      R"({"decision": "move", "target": "new_task", "confidence": 1.0})")));
  // Confidence out of range.
  EXPECT_FALSE(ParseClassificationResponse(WrapContent(
      R"({"decision": "leave", "confidence": 2.0})")));
  // Prose instead of the JSON object.
  EXPECT_FALSE(ParseClassificationResponse(WrapContent(
      R"("I think the branch belongs to Research.")")));
}

TEST_F(AiProtocolTest, ModelListParsesIds) {
  auto models = ParseModelList(
      R"({"data": [{"id": "llama3.2"}, {"id": "qwen2.5"}]})");
  ASSERT_TRUE(models);
  EXPECT_EQ(*models, (std::vector<std::u16string>{u"llama3.2", u"qwen2.5"}));

  models = ParseModelList(R"({"data": []})");
  ASSERT_TRUE(models);
  EXPECT_TRUE(models->empty());

  EXPECT_FALSE(ParseModelList(R"({"data": [{"name": "no-id"}]})"));
  EXPECT_FALSE(ParseModelList("broken"));
}

}  // namespace
}  // namespace uw
