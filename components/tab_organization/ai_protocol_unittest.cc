// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/components/tab_organization/ai_protocol.h"

#include "base/json/json_reader.h"
#include "base/test/task_environment.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace uw {
namespace {

class AiProtocolTest : public testing::Test {
 protected:
  base::test::TaskEnvironment environment_;
};

TEST_F(AiProtocolTest, JevRequestRoundTripsMetadata) {
  auto body = BuildJevRequestBody(
      "Benchmark Jev", "https://example.com/bench", "opened from tab A",
      {{"task-1", "Research"}, {kJevNewTaskOption, "Create a new task"},
       {kJevScratchpadOption, "Leave ungrouped"}});
  ASSERT_TRUE(body);
  auto parsed = base::JSONReader::ReadDict(*body, base::JSON_PARSE_RFC);
  ASSERT_TRUE(parsed);
  EXPECT_EQ(*parsed->FindStringByDottedPath("model"), kJevModel);
  EXPECT_EQ(*parsed->FindStringByDottedPath("state.title"), "Benchmark Jev");
  EXPECT_EQ(*parsed->FindStringByDottedPath("state.url"),
            "https://example.com/bench");
  EXPECT_EQ(*parsed->FindStringByDottedPath(
                "questions.destination.criteria.task-1"),
            "Research");
}

TEST_F(AiProtocolTest, JevRequestRejectsEmptyInput) {
  EXPECT_FALSE(BuildJevRequestBody("", "https://example.com", "source", {}));
  EXPECT_FALSE(BuildJevRequestBody("Title", "https://example.com", "source",
                                   {{}}));
}

TEST_F(AiProtocolTest, JevChoiceParsesAndValidates) {
  constexpr char kResponse[] = R"({
    "model": "jev-1.13.0",
    "answers": {
      "destination": {
        "type": "choice",
        "choice": "task-1",
        "probabilities": {"task-1": 0.93, "new_task": 0.07},
        "confidence": 0.91
      }
    },
    "usage": {"input_tokens": 100, "output_tokens": 10}
  })";
  auto answer = ParseJevChoiceResponse(kResponse, "destination");
  ASSERT_TRUE(answer);
  EXPECT_EQ(answer->choice, "task-1");
  EXPECT_DOUBLE_EQ(answer->confidence, 0.91);
  EXPECT_DOUBLE_EQ(answer->probabilities.at("task-1"), 0.93);
}

TEST_F(AiProtocolTest, JevChoiceRejectsMalformedResponses) {
  // Wrong question id.
  EXPECT_FALSE(ParseJevChoiceResponse(R"({"answers": {}})", "destination"));
  // Choice outside the probability map.
  EXPECT_FALSE(ParseJevChoiceResponse(
      R"({"answers": {"destination": {"type": "choice", "choice": "other",
          "probabilities": {"task-1": 1.0}, "confidence": 1.0}}})",
      "destination"));
  // Probabilities do not sum to one.
  EXPECT_FALSE(ParseJevChoiceResponse(
      R"({"answers": {"destination": {"type": "choice", "choice": "task-1",
          "probabilities": {"task-1": 0.5}, "confidence": 0.5}}})",
      "destination"));
  // Confidence out of range.
  EXPECT_FALSE(ParseJevChoiceResponse(
      R"({"answers": {"destination": {"type": "choice", "choice": "task-1",
          "probabilities": {"task-1": 1.0}, "confidence": 2.0}}})",
      "destination"));
  // Not JSON.
  EXPECT_FALSE(ParseJevChoiceResponse("not json", "destination"));
}

TEST_F(AiProtocolTest, NamingRequestAndResponse) {
  auto body = BuildNamingRequestBody("test-model", "Name this task");
  ASSERT_TRUE(body);
  auto parsed = base::JSONReader::ReadDict(*body, base::JSON_PARSE_RFC);
  ASSERT_TRUE(parsed);
  EXPECT_EQ(*parsed->FindString("model"), "test-model");

  constexpr char kResponse[] = R"({
    "choices": [{"message": {"role": "assistant", "content": "  Benchmark Jev  "}}]
  })";
  EXPECT_EQ(ParseNamingResponse(kResponse), u"Benchmark Jev");
  EXPECT_FALSE(
      ParseNamingResponse(R"({"choices": [{"message": {"content": "   "}}]})"));
  EXPECT_FALSE(ParseNamingResponse(R"({"choices": []})"));
  EXPECT_FALSE(ParseNamingResponse("broken"));
}

}  // namespace
}  // namespace uw
