// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/components/agent/memory_check.h"

#include <optional>
#include <string>

#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/strings/strcat.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace uw {
namespace {

using ::testing::ElementsAre;
using ::testing::HasSubstr;

MemoryCheckInput Partner() {
  MemoryCheckInput input;
  input.user = {"Book a table for four this Friday at 7, somewhere near home.",
                "My partner is vegetarian now, so it needs good options."};
  input.memory = "Partner is vegetarian.";
  return input;
}

base::DictValue Body(DecisionFormat format, const MemoryCheckInput& input) {
  const std::optional<std::string> body =
      BuildMemoryCheckRequestBody(format, "check-model", input);
  EXPECT_TRUE(body);
  std::optional<base::DictValue> dict =
      base::JSONReader::ReadDict(body.value_or("{}"), base::JSON_PARSE_RFC);
  EXPECT_TRUE(dict);
  return dict ? std::move(*dict) : base::DictValue();
}

// A language model's answer whose text is `json`.
std::string ChatAnswer(std::string_view json) {
  return base::WriteJson(
             base::DictValue().Set(
                 "choices", base::ListValue().Append(base::DictValue().Set(
                                "message", base::DictValue()
                                               .Set("role", "assistant")
                                               .Set("content", json)))))
      .value_or("");
}

TEST(MemoryCheckTest, ADecisionModelGetsWhatTheUserWroteAndTwoQuestions) {
  const base::DictValue body = Body(DecisionFormat::kDecisions, Partner());

  EXPECT_EQ(*body.FindString("model"), "check-model");
  EXPECT_EQ(*body.FindString("input"),
            "What the user wrote in this chat, oldest first:\n"
            "- Book a table for four this Friday at 7, somewhere near home.\n"
            "- My partner is vegetarian now, so it needs good options.\n\n"
            "The sentence to remember:\nPartner is vegetarian.");
  const base::ListValue& questions = *body.FindList("questions");
  ASSERT_EQ(questions.size(), 2u);
  const auto values = [&questions](size_t index) {
    std::vector<std::string> values;
    for (const base::Value& choice :
         *questions[index].GetDict().FindList("choices")) {
      values.push_back(*choice.GetDict().FindString("value"));
    }
    return values;
  };
  EXPECT_EQ(*questions[0].GetDict().FindString("name"), "source");
  EXPECT_THAT(values(0), ElementsAre("user", "elsewhere"));
  EXPECT_EQ(*questions[1].GetDict().FindString("name"), "content");
  EXPECT_THAT(values(1), ElementsAre("ordinary", "secret", "sensitive"));
}

TEST(MemoryCheckTest, SystemOneGetsTheSameQuestions) {
  const base::DictValue body = Body(DecisionFormat::kSystemOne, Partner());

  EXPECT_THAT(*body.FindString("state"), HasSubstr("Partner is vegetarian."));
  const base::DictValue& questions = *body.FindDict("questions");
  EXPECT_TRUE(questions.FindDict("source"));
  EXPECT_TRUE(questions.FindStringByDottedPath("content.criteria.secret"));
}

TEST(MemoryCheckTest, ALanguageModelIsToldTheShapeOfItsAnswer) {
  const base::DictValue body = Body(DecisionFormat::kChat, Partner());

  const base::ListValue& messages = *body.FindList("messages");
  ASSERT_EQ(messages.size(), 2u);
  EXPECT_THAT(*messages[0].GetDict().FindString("content"),
              HasSubstr(R"("source_confidence": <number 0..1>)"));
  EXPECT_THAT(*messages[1].GetDict().FindString("content"),
              HasSubstr("My partner is vegetarian now"));
}

TEST(MemoryCheckTest, OnlyTheNewestMessagesThatFitAreSent) {
  MemoryCheckInput input = Partner();
  input.user.assign(20, std::string(900, 'x'));
  input.user.push_back("I moved to Oakland last week.");
  input.memory = "Lives in Oakland.";

  const std::string sent =
      *Body(DecisionFormat::kDecisions, input).FindString("input");

  EXPECT_THAT(sent, HasSubstr("earlier messages left out]"));
  EXPECT_THAT(sent, HasSubstr("- I moved to Oakland last week.\n"));
  EXPECT_LT(sent.size(), 8300u);
}

TEST(MemoryCheckTest, NoSentenceAsksNothing) {
  MemoryCheckInput input = Partner();
  input.memory.clear();
  EXPECT_FALSE(BuildMemoryCheckRequestBody(DecisionFormat::kChat, "m", input));
  EXPECT_FALSE(
      BuildMemoryCheckRequestBody(DecisionFormat::kChat, "", Partner()));
}

TEST(MemoryCheckTest, WhatTheUserSaidIsKept) {
  const std::optional<MemoryCheckAnswer> answer = ParseMemoryCheckResponse(
      DecisionFormat::kChat,
      ChatAnswer(R"({"source": "user", "source_confidence": 0.95, )"
                 R"("content": "ordinary", "content_confidence": 0.9})"));

  ASSERT_TRUE(answer);
  EXPECT_DOUBLE_EQ(answer->from_user, 0.95);
  EXPECT_DOUBLE_EQ(answer->ordinary, 0.9);
  EXPECT_TRUE(JudgeMemory(answer).keep);
}

TEST(MemoryCheckTest, AnAnswerInACodeFenceIsRead) {
  const std::optional<MemoryCheckAnswer> answer = ParseMemoryCheckResponse(
      DecisionFormat::kChat,
      ChatAnswer("```json\n{\"source\": \"user\", \"source_confidence\": 1, "
                 "\"content\": \"ordinary\", \"content_confidence\": 1}\n```"));

  ASSERT_TRUE(answer);
  EXPECT_TRUE(JudgeMemory(answer).keep);
}

TEST(MemoryCheckTest, WhatAPageSaidIsNotKept) {
  const std::optional<MemoryCheckAnswer> answer = ParseMemoryCheckResponse(
      DecisionFormat::kChat,
      ChatAnswer(R"({"source": "elsewhere", "source_confidence": 0.9, )"
                 R"("content": "ordinary", "content_confidence": 0.9})"));

  ASSERT_TRUE(answer);
  const MemoryCheckVerdict verdict = JudgeMemory(answer);
  EXPECT_FALSE(verdict.keep);
  EXPECT_THAT(verdict.reason, HasSubstr("the user didn't say this"));
}

TEST(MemoryCheckTest, ASecretIsNotKeptEvenWhenTheUserSaidIt) {
  const std::optional<MemoryCheckAnswer> answer = ParseMemoryCheckResponse(
      DecisionFormat::kChat,
      ChatAnswer(R"({"source": "user", "source_confidence": 0.99, )"
                 R"("content": "secret", "content_confidence": 0.95})"));

  ASSERT_TRUE(answer);
  EXPECT_EQ(answer->holds, "secret");
  const MemoryCheckVerdict verdict = JudgeMemory(answer);
  EXPECT_FALSE(verdict.keep);
  EXPECT_THAT(verdict.reason, HasSubstr("holds a secret"));
}

TEST(MemoryCheckTest, ASensitiveTopicIsNotKeptUnlessAsked) {
  const MemoryCheckVerdict verdict = JudgeMemory(ParseMemoryCheckResponse(
      DecisionFormat::kChat,
      ChatAnswer(R"({"source": "user", "source_confidence": 0.9, )"
                 R"("content": "sensitive", "content_confidence": 0.8})")));

  EXPECT_FALSE(verdict.keep);
  EXPECT_THAT(verdict.reason, HasSubstr("sensitive topic"));
}

TEST(MemoryCheckTest, ADecisionModelsProbabilitiesAreRead) {
  const std::optional<MemoryCheckAnswer> answer =
      ParseMemoryCheckResponse(DecisionFormat::kDecisions, R"({"answers":[
        {"type":"choice","name":"source","choice":"user","probabilities":[
          {"value":"user","probability":0.93},
          {"value":"elsewhere","probability":0.07}]},
        {"type":"choice","name":"content","choice":"ordinary",
         "probabilities":[{"value":"ordinary","probability":0.88},
          {"value":"secret","probability":0.02},
          {"value":"sensitive","probability":0.1}]}]})");

  ASSERT_TRUE(answer);
  EXPECT_DOUBLE_EQ(answer->from_user, 0.93);
  EXPECT_DOUBLE_EQ(answer->ordinary, 0.88);
  EXPECT_EQ(answer->holds, "ordinary");
  EXPECT_TRUE(JudgeMemory(answer).keep);
}

TEST(MemoryCheckTest, SystemOnesAnswerIsRead) {
  const std::optional<MemoryCheckAnswer> answer =
      ParseMemoryCheckResponse(DecisionFormat::kSystemOne, R"({"answers":{
        "source":{"choice":"elsewhere",
                  "probabilities":{"user":0.3,"elsewhere":0.7}},
        "content":{"choice":"ordinary",
                   "probabilities":{"ordinary":0.9,"secret":0.05,
                                    "sensitive":0.05}}}})");

  ASSERT_TRUE(answer);
  EXPECT_DOUBLE_EQ(answer->from_user, 0.3);
  EXPECT_FALSE(JudgeMemory(answer).keep);
}

TEST(MemoryCheckTest, AnUnsureOrMissingAnswerKeepsNothing) {
  for (const char* body :
       {"{}", R"({"answers":[]})",
        R"({"answers":[{"name":"source","choice":"user","confidence":0.9},
                       {"name":"content","choice":"maybe","confidence":0.9}]})",
        R"({"answers":[{"name":"source","choice":"user","confidence":2},
                       {"name":"content","choice":"ordinary",
                        "confidence":0.9}]})"}) {
    EXPECT_FALSE(ParseMemoryCheckResponse(DecisionFormat::kDecisions, body))
        << body;
  }
  EXPECT_FALSE(ParseMemoryCheckResponse(DecisionFormat::kChat,
                                        ChatAnswer("I would keep it.")));
  const MemoryCheckVerdict verdict = JudgeMemory(std::nullopt);
  EXPECT_FALSE(verdict.keep);
  EXPECT_THAT(verdict.reason, HasSubstr("didn't answer"));
  const MemoryCheckVerdict unsure = JudgeMemory(ParseMemoryCheckResponse(
      DecisionFormat::kChat,
      ChatAnswer(R"({"source": "user", "source_confidence": 0.6, )"
                 R"("content": "ordinary", "content_confidence": 0.9})")));
  EXPECT_FALSE(unsure.keep);
}

}  // namespace
}  // namespace uw
