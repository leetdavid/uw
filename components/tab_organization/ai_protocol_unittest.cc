// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/components/tab_organization/ai_protocol.h"

#include <string>
#include <utility>
#include <vector>

#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/strings/string_number_conversions.h"
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
  const std::string* model_name = parsed->FindString("model");
  ASSERT_TRUE(model_name);
  EXPECT_EQ(*model_name, "test-model");
  const base::ListValue* messages = parsed->FindList("messages");
  ASSERT_TRUE(messages);
  ASSERT_EQ(messages->size(), 2u);
  const base::DictValue* user_message = (*messages)[1].GetIfDict();
  ASSERT_TRUE(user_message);
  const std::string* user_content = user_message->FindString("content");
  ASSERT_TRUE(user_content);
  auto description =
      base::JSONReader::ReadDict(*user_content, base::JSON_PARSE_RFC);
  ASSERT_TRUE(description);
  const std::string* title = description->FindString("title");
  ASSERT_TRUE(title);
  EXPECT_EQ(*title, "Benchmark Jev");
  const std::string* url = description->FindString("url");
  ASSERT_TRUE(url);
  EXPECT_EQ(*url, "https://example.com/bench");
  const std::string* source = description->FindString("source");
  ASSERT_TRUE(source);
  EXPECT_EQ(*source, "opened from tab A");
  const std::string* excerpt = description->FindString("excerpt");
  ASSERT_TRUE(excerpt);
  EXPECT_EQ(*excerpt, "A short readable excerpt.");
  const base::ListValue* tasks = description->FindList("tasks");
  ASSERT_TRUE(tasks);
  ASSERT_EQ(tasks->size(), 2u);
  const base::DictValue* first_task = (*tasks)[0].GetIfDict();
  ASSERT_TRUE(first_task);
  const std::string* first_key = first_task->FindString("key");
  ASSERT_TRUE(first_key);
  EXPECT_EQ(*first_key, "task-1");
  const base::DictValue* second_task = (*tasks)[1].GetIfDict();
  ASSERT_TRUE(second_task);
  const std::string* second_name = second_task->FindString("name");
  ASSERT_TRUE(second_name);
  EXPECT_EQ(*second_name, "Travel");
}

TEST_F(AiProtocolTest, ClassificationRequestRejectsEmptyInput) {
  EXPECT_FALSE(BuildClassificationRequestBody("", BranchContext(), {}));
  EXPECT_FALSE(BuildClassificationRequestBody("model", BranchContext(), {}));
}

TEST_F(AiProtocolTest, ClassificationParsesAndValidates) {
  auto answer = ParseClassificationResponse(WrapContent(R"({
    "decision": "move", "target": "task-1", "confidence": 0.93})"));
  ASSERT_TRUE(answer);
  EXPECT_EQ(answer->choice, "task-1");
  EXPECT_DOUBLE_EQ(answer->confidence, 0.93);
  EXPECT_FALSE(answer->task_name);

  answer = ParseClassificationResponse(WrapContent(
      R"({"decision": "new_task", "confidence": 0.95, "name": "  Benchmark Jev  "})"));
  ASSERT_TRUE(answer);
  EXPECT_EQ(answer->choice, kNewTaskOption);
  EXPECT_EQ(*answer->task_name, u"Benchmark Jev");

  answer = ParseClassificationResponse(
      WrapContent(R"({"decision": "leave", "confidence": 0.4})"));
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
    "decision": "new_task", "confidence": 0.95, "name": "  "})"));
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
  EXPECT_FALSE(
      ParseClassificationResponse(R"({"choices": [{"message": {}}]})"));
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
  EXPECT_FALSE(ParseClassificationResponse(
      WrapContent(R"({"decision": "leave", "confidence": 2.0})")));
  // Prose instead of the JSON object.
  EXPECT_FALSE(ParseClassificationResponse(
      WrapContent(R"("I think the branch belongs to Research.")")));
}

TEST_F(AiProtocolTest, ModelListParsesIds) {
  auto models =
      ParseModelList(R"({"data": [{"id": "llama3.2"}, {"id": "qwen2.5"}]})");
  ASSERT_TRUE(models);
  EXPECT_EQ(*models, (std::vector<std::u16string>{u"llama3.2", u"qwen2.5"}));

  models = ParseModelList(R"({"data": []})");
  ASSERT_TRUE(models);
  EXPECT_TRUE(models->empty());

  EXPECT_FALSE(ParseModelList(R"({"data": [{"name": "no-id"}]})"));
  EXPECT_FALSE(ParseModelList("broken"));
}

BranchContext LisbonContext() {
  BranchContext context;
  context.title = "Lisbon in three days";
  context.url = "https://example.com/lisbon";
  context.source_description = "opened from \"Hacker News\"";
  context.excerpt = "Day one: Alfama and the castle.";
  return context;
}

TEST_F(AiProtocolTest, DecisionsRequestAsksOneChoiceQuestion) {
  auto body = BuildDecisionRequestBody(
      DecisionFormat::kDecisions, "gpt-6-luna", LisbonContext(),
      {{"task-1", "Plan Lisbon trip"}, {"task-2", "Benchmark models"}});
  ASSERT_TRUE(body);
  auto parsed = base::JSONReader::ReadDict(*body, base::JSON_PARSE_RFC);
  ASSERT_TRUE(parsed);
  EXPECT_EQ(*parsed->FindString("model"), "gpt-6-luna");
  const std::string* input = parsed->FindString("input");
  ASSERT_TRUE(input);
  EXPECT_NE(input->find("Title: Lisbon in three days"), std::string::npos);
  EXPECT_NE(input->find("Page excerpt: Day one"), std::string::npos);
  const base::ListValue* questions = parsed->FindList("questions");
  ASSERT_TRUE(questions);
  ASSERT_EQ(questions->size(), 1u);
  const base::DictValue& question = (*questions)[0].GetDict();
  EXPECT_EQ(*question.FindString("type"), "choice");
  EXPECT_EQ(*question.FindString("name"), "destination");
  EXPECT_FALSE(question.FindString("instructions")->empty());
  const base::ListValue* choices = question.FindList("choices");
  ASSERT_TRUE(choices);
  ASSERT_EQ(choices->size(), 4u);
  EXPECT_EQ(*(*choices)[0].GetDict().FindString("value"), "task-1");
  EXPECT_EQ(*(*choices)[0].GetDict().FindString("description"),
            "Plan Lisbon trip");
  EXPECT_EQ(*(*choices)[2].GetDict().FindString("value"), kNewTaskOption);
  EXPECT_EQ(*(*choices)[3].GetDict().FindString("value"), kScratchpadOption);
}

TEST_F(AiProtocolTest, SystemOneRequestListsTasksAsCriteria) {
  auto body = BuildDecisionRequestBody(DecisionFormat::kSystemOne, "jev-latest",
                                       LisbonContext(),
                                       {{"task-1", "Plan Lisbon trip"}});
  ASSERT_TRUE(body);
  auto parsed = base::JSONReader::ReadDict(*body, base::JSON_PARSE_RFC);
  ASSERT_TRUE(parsed);
  EXPECT_EQ(*parsed->FindString("model"), "jev-latest");
  const std::string* state = parsed->FindString("state");
  ASSERT_TRUE(state);
  EXPECT_NE(state->find("Source: opened from"), std::string::npos);
  const base::DictValue* question =
      parsed->FindDict("questions")->FindDict("destination");
  ASSERT_TRUE(question);
  EXPECT_EQ(*question->FindString("type"), "choice");
  const base::DictValue* criteria = question->FindDict("criteria");
  ASSERT_TRUE(criteria);
  EXPECT_EQ(criteria->size(), 3u);
  EXPECT_EQ(*criteria->FindString("task-1"), "Plan Lisbon trip");
  EXPECT_TRUE(criteria->FindString(kNewTaskOption));
  EXPECT_TRUE(criteria->FindString(kScratchpadOption));
}

TEST_F(AiProtocolTest, DecisionRequestRejectsBadInput) {
  const BranchContext context = LisbonContext();
  for (DecisionFormat format :
       {DecisionFormat::kDecisions, DecisionFormat::kSystemOne}) {
    EXPECT_FALSE(BuildDecisionRequestBody(format, "", context, {}));
    EXPECT_FALSE(
        BuildDecisionRequestBody(format, "model", BranchContext(), {}));
    EXPECT_FALSE(BuildDecisionRequestBody(format, "model", context,
                                          {{kScratchpadOption, "x"}}));
    std::vector<std::pair<std::string, std::string>> tasks;
    for (int i = 0; i < 253; ++i) {
      tasks.emplace_back("task-" + base::NumberToString(i), "Task");
    }
    EXPECT_TRUE(BuildDecisionRequestBody(format, "model", context, tasks));
    tasks.emplace_back("one-too-many", "Task");
    EXPECT_FALSE(BuildDecisionRequestBody(format, "model", context, tasks));
  }
}

TEST_F(AiProtocolTest, DecisionsResponseParsesTheDestinationAnswer) {
  auto answer = ParseDecisionResponse(DecisionFormat::kDecisions, R"({
    "model": "gpt-6-luna",
    "answers": [
      {"type": "predicate", "name": "other", "probability": 0.2},
      {"type": "choice", "name": "destination", "choice": "task-1",
       "confidence": 0.94,
       "probabilities": [{"value": "task-1", "probability": 0.94},
                         {"value": "scratchpad", "probability": 0.06}]}
    ]})");
  ASSERT_TRUE(answer);
  EXPECT_EQ(answer->choice, "task-1");
  EXPECT_DOUBLE_EQ(answer->confidence, 0.94);
  EXPECT_FALSE(answer->task_name);

  // Without a confidence field, the chosen value's probability stands in.
  answer = ParseDecisionResponse(DecisionFormat::kDecisions, R"({"answers": [
      {"type": "choice", "name": "destination", "choice": "new_task",
       "probabilities": [{"value": "new_task", "probability": 0.91}]}]})");
  ASSERT_TRUE(answer);
  EXPECT_EQ(answer->choice, kNewTaskOption);
  EXPECT_DOUBLE_EQ(answer->confidence, 0.91);
}

TEST_F(AiProtocolTest, DecisionsResponseRejectsUnusableAnswers) {
  const auto parse = [](const char* body) {
    return ParseDecisionResponse(DecisionFormat::kDecisions, body);
  };
  EXPECT_FALSE(parse("not json"));
  EXPECT_FALSE(parse(R"({"answers": []})"));
  EXPECT_FALSE(
      parse(R"({"answers": [{"type": "refusal", "name": "destination"}]})"));
  EXPECT_FALSE(parse(R"({"answers": [
      {"type": "choice", "name": "another", "choice": "task-1", "confidence": 1}]})"));
  EXPECT_FALSE(parse(R"({"answers": [
      {"type": "choice", "name": "destination", "choice": "task-1"}]})"));
  EXPECT_FALSE(parse(R"({"answers": [
      {"type": "choice", "name": "destination", "choice": "task-1", "confidence": 1.5}]})"));
  EXPECT_FALSE(parse(R"({"answers": [
      {"type": "choice", "name": "destination", "choice": true, "confidence": 0.99}]})"));
}

TEST_F(AiProtocolTest, SystemOneResponseParsesTheDestinationAnswer) {
  auto answer = ParseDecisionResponse(DecisionFormat::kSystemOne, R"({
    "model": "jev-latest",
    "answers": {"destination": {
      "type": "choice", "choice": "scratchpad", "confidence": 0.72,
      "probabilities": {"scratchpad": 0.72, "task-1": 0.28}}}})");
  ASSERT_TRUE(answer);
  EXPECT_EQ(answer->choice, kScratchpadOption);
  EXPECT_DOUBLE_EQ(answer->confidence, 0.72);

  // A fallback language model reports no confidence as zero.
  answer = ParseDecisionResponse(DecisionFormat::kSystemOne, R"({"answers": {
      "destination": {"type": "choice", "choice": "task-1", "confidence": 0,
                      "probabilities": {}}}})");
  ASSERT_TRUE(answer);
  EXPECT_DOUBLE_EQ(answer->confidence, 0.0);

  EXPECT_FALSE(
      ParseDecisionResponse(DecisionFormat::kSystemOne, R"({"answers": {}})"));
  EXPECT_FALSE(ParseDecisionResponse(
      DecisionFormat::kSystemOne,
      R"({"answers": {"destination": {"type": "noul", "noul": 0.9}}})"));
  EXPECT_FALSE(ParseDecisionResponse(
      DecisionFormat::kSystemOne,
      R"({"answers": {"destination": {"type": "choice", "choice": "task-1"}}})"));
}

TEST_F(AiProtocolTest, ChatFormatUsesTheClassificationCall) {
  const BranchContext context = LisbonContext();
  EXPECT_EQ(BuildDecisionRequestBody(DecisionFormat::kChat, "model", context,
                                     {{"task-1", "Plan Lisbon trip"}}),
            BuildClassificationRequestBody("model", context,
                                           {{"task-1", "Plan Lisbon trip"}}));
  auto answer = ParseDecisionResponse(
      DecisionFormat::kChat,
      WrapContent(
          R"({"decision": "new_task", "confidence": 0.95, "name": "Plan trip"})"));
  ASSERT_TRUE(answer);
  EXPECT_EQ(answer->choice, kNewTaskOption);
  EXPECT_EQ(*answer->task_name, u"Plan trip");
}

TEST_F(AiProtocolTest, NamingCallAsksForAndCleansAShortName) {
  EXPECT_FALSE(BuildNamingRequestBody("", LisbonContext()));
  EXPECT_FALSE(BuildNamingRequestBody("model", BranchContext()));
  auto body = BuildNamingRequestBody("llama3.2", LisbonContext());
  ASSERT_TRUE(body);
  auto parsed = base::JSONReader::ReadDict(*body, base::JSON_PARSE_RFC);
  ASSERT_TRUE(parsed);
  EXPECT_EQ(*parsed->FindString("model"), "llama3.2");
  const base::ListValue* messages = parsed->FindList("messages");
  ASSERT_TRUE(messages);
  ASSERT_EQ(messages->size(), 2u);
  EXPECT_NE((*messages)[1].GetDict().FindString("content")->find(
                "Title: Lisbon in three days"),
            std::string::npos);

  EXPECT_EQ(ParseNamingResponse(
                WrapContent("\"Plan Lisbon trip\"\n\nThis name fits because.")),
            u"Plan Lisbon trip");
  EXPECT_FALSE(ParseNamingResponse(WrapContent("  \n")));
  EXPECT_FALSE(ParseNamingResponse("not json"));
  EXPECT_EQ(ParseNamingResponse(WrapContent(std::string(200, 'a')))->size(),
            kMaxGeneratedTaskNameLength);
}

}  // namespace
}  // namespace uw
