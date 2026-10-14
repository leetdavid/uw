// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/components/tab_organization/ai_protocol.h"

#include <optional>
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
    "task": "task-1", "confidence": 0.93, "new_task": 0.1})"));
  ASSERT_TRUE(answer);
  EXPECT_EQ(answer->choice, "task-1");
  EXPECT_DOUBLE_EQ(answer->confidence, 0.93);
  EXPECT_FALSE(answer->task_name);

  answer = ParseClassificationResponse(WrapContent(
      R"({"task": "none", "confidence": 0.95, "new_task": 0.8, "name": "  Benchmark Jev  "})"));
  ASSERT_TRUE(answer);
  EXPECT_EQ(answer->choice, kNewTaskOption);
  EXPECT_DOUBLE_EQ(answer->confidence, 0.95);
  EXPECT_DOUBLE_EQ(answer->new_task, 0.8);
  EXPECT_EQ(*answer->task_name, u"Benchmark Jev");

  // A tab outside every task that is less likely than not to start one.
  answer = ParseClassificationResponse(WrapContent(
      R"({"task": "none", "confidence": 0.4, "new_task": 0.3, "name": "Sign in"})"));
  ASSERT_TRUE(answer);
  EXPECT_EQ(answer->choice, kScratchpadOption);
  EXPECT_DOUBLE_EQ(answer->new_task, 0.3);
  EXPECT_FALSE(answer->task_name);
}

TEST_F(AiProtocolTest, ClassificationAcceptsCodeFencedContent) {
  auto answer = ParseClassificationResponse(WrapContent(
      "```\n{\"task\": \"none\", \"confidence\": 1.0, \"new_task\": 0}\n```"));
  ASSERT_TRUE(answer);
  EXPECT_EQ(answer->choice, kScratchpadOption);
}

TEST_F(AiProtocolTest, NewTaskNameIsCappedAndOptional) {
  auto answer = ParseClassificationResponse(WrapContent(R"({
    "task": "none", "confidence": 0.95, "new_task": 0.9, "name": "  "})"));
  ASSERT_TRUE(answer);
  EXPECT_FALSE(answer->task_name);

  const std::string long_name(200, 'a');
  answer = ParseClassificationResponse(
      WrapContent("{\"task\": \"none\", \"confidence\": 0.95, "
                  "\"new_task\": 0.9, \"name\": \"" +
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
  // No task.
  EXPECT_FALSE(ParseClassificationResponse(
      WrapContent(R"({"confidence": 1.0, "new_task": 0.5})")));
  // A task without a confidence.
  EXPECT_FALSE(
      ParseClassificationResponse(WrapContent(R"({"task": "task-1"})")));
  // A reserved option key as the task.
  EXPECT_FALSE(ParseClassificationResponse(WrapContent(
      R"({"task": "new_task", "confidence": 1.0, "new_task": 1.0})")));
  // Outside every task, without saying whether it starts one.
  EXPECT_FALSE(ParseClassificationResponse(
      WrapContent(R"({"task": "none", "confidence": 0.9})")));
  // Numbers out of range.
  EXPECT_FALSE(ParseClassificationResponse(
      WrapContent(R"({"task": "none", "confidence": 2.0, "new_task": 0.5})")));
  EXPECT_FALSE(ParseClassificationResponse(
      WrapContent(R"({"task": "none", "confidence": 0.9, "new_task": 1.5})")));
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

// The body api.typesafe.ai returned for GET /v1/models on 2026-10-12.
TEST_F(AiProtocolTest, ModelListParsesTypeSafeNames) {
  const auto models = ParseModelList(
      R"({"models":[{"name":"jev-latest","description":"The latest iteration )"
      R"(of TypeSafe's System One Model: Jev","release_date":)"
      R"("2026-09-10T18:38:01.391457+00:00"},{"name":"jev-preview",)"
      R"("description":"A preview version of `jev-latest`: should be better )"
      R"(in most ways","release_date":"2026-09-10T18:39:06.057655+00:00"}]})");
  ASSERT_TRUE(models);
  EXPECT_EQ(*models,
            (std::vector<std::u16string>{u"jev-latest", u"jev-preview"}));

  EXPECT_FALSE(ParseModelList(R"({"models": [{"id": "no-name"}]})"));
}

// The bodies api.typesafe.ai returned on 2026-10-12 for a tab that continues
// a task, a sign-in page that belongs to none, and the same page asked about
// with no tasks.
TEST_F(AiProtocolTest, SystemOneParsesRecordedLiveAnswers) {
  auto answer = ParseDecisionResponse(
      DecisionFormat::kSystemOne,
      R"({"model":"jev-1.13.0","answers":{"destination":{"type":"choice",)"
      R"("choice":"task-1","confidence":0.82,"probabilities":{"task-1":0.88,)"
      R"("task-2":0.0,"none":0.12}},"otherwise":{"type":"choice","choice":)"
      R"("new_task","confidence":0.72,"probabilities":{"new_task":0.86,)"
      R"("scratchpad":0.14}}},"usage":{"input_tokens":519,"output_tokens":77}})",
      /*has_tasks=*/true);
  ASSERT_TRUE(answer);
  EXPECT_EQ(answer->choice, "task-1");
  EXPECT_DOUBLE_EQ(answer->confidence, 0.82);
  EXPECT_FALSE(answer->task_name);

  answer = ParseDecisionResponse(
      DecisionFormat::kSystemOne,
      R"({"model":"jev-1.13.0","answers":{"destination":{"type":"choice",)"
      R"("choice":"none","confidence":1.0,"probabilities":{"task-1":0.0,)"
      R"("task-2":0.0,"none":1.0}},"otherwise":{"type":"choice","choice":)"
      R"("scratchpad","confidence":0.98,"probabilities":{"new_task":0.01,)"
      R"("scratchpad":0.99}}},"usage":{"input_tokens":523,"output_tokens":77}})",
      /*has_tasks=*/true);
  ASSERT_TRUE(answer);
  EXPECT_EQ(answer->choice, kScratchpadOption);
  EXPECT_DOUBLE_EQ(answer->confidence, 1.0);
  EXPECT_DOUBLE_EQ(answer->new_task, 0.01);

  answer = ParseDecisionResponse(
      DecisionFormat::kSystemOne,
      R"({"model":"jev-1.13.0","answers":{"otherwise":{"type":"choice",)"
      R"("choice":"scratchpad","confidence":0.98,"probabilities":)"
      R"({"new_task":0.01,"scratchpad":0.99}}},"usage":{"input_tokens":417,)"
      R"("output_tokens":38}})",
      /*has_tasks=*/false);
  ASSERT_TRUE(answer);
  EXPECT_EQ(answer->choice, kScratchpadOption);
  EXPECT_DOUBLE_EQ(answer->confidence, 1.0);
  EXPECT_DOUBLE_EQ(answer->new_task, 0.01);
}

BranchContext LisbonContext() {
  BranchContext context;
  context.title = "Lisbon in three days";
  context.url = "https://example.com/lisbon";
  context.source_description = "opened from \"Hacker News\"";
  context.excerpt = "Day one: Alfama and the castle.";
  return context;
}

TEST_F(AiProtocolTest, ATerminalIsDescribedByItsFolder) {
  BranchContext context;
  context.title = "pnpm build";
  context.url = "chrome://terminal/";
  context.source_description = "no source tab";
  context.folder = "~/dev/uw";
  auto state = BuildDecisionRequestBody(DecisionFormat::kSystemOne, "jev",
                                        context, {{"task-1", "Build uw"}});
  ASSERT_TRUE(state);
  EXPECT_NE(state->find("Folder: ~/dev/uw"), std::string::npos);
  EXPECT_EQ(state->find("Page excerpt"), std::string::npos);
  auto chat = BuildClassificationRequestBody("test-model", context,
                                             {{"task-1", "Build uw"}});
  ASSERT_TRUE(chat);
  EXPECT_NE(chat->find(R"(\"folder\":\"~/dev/uw\")"), std::string::npos);
}

TEST_F(AiProtocolTest, DecisionsRequestAsksWhichTaskAndWhatElse) {
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
  ASSERT_EQ(questions->size(), 3u);
  const base::DictValue& destination = (*questions)[0].GetDict();
  EXPECT_EQ(*destination.FindString("type"), "choice");
  EXPECT_EQ(*destination.FindString("name"), "destination");
  EXPECT_FALSE(destination.FindString("instructions")->empty());
  const base::ListValue* choices = destination.FindList("choices");
  ASSERT_TRUE(choices);
  ASSERT_EQ(choices->size(), 3u);
  EXPECT_EQ(*(*choices)[0].GetDict().FindString("value"), "task-1");
  EXPECT_EQ(*(*choices)[0].GetDict().FindString("description"),
            "Plan Lisbon trip");
  EXPECT_EQ(*(*choices)[2].GetDict().FindString("value"), kNoTaskOption);

  const base::DictValue& otherwise = (*questions)[1].GetDict();
  EXPECT_EQ(*otherwise.FindString("name"), "otherwise");
  const base::ListValue* kinds = otherwise.FindList("choices");
  ASSERT_TRUE(kinds);
  ASSERT_EQ(kinds->size(), 2u);
  EXPECT_EQ(*(*kinds)[0].GetDict().FindString("value"), kNewTaskOption);
  EXPECT_EQ(*(*kinds)[1].GetDict().FindString("value"), kScratchpadOption);

  const base::DictValue& kind = (*questions)[2].GetDict();
  EXPECT_EQ(*kind.FindString("name"), "kind");
  const base::ListValue* pages = kind.FindList("choices");
  ASSERT_TRUE(pages);
  ASSERT_EQ(pages->size(), 2u);
  EXPECT_EQ(*(*pages)[0].GetDict().FindString("value"), "hub");
  EXPECT_EQ(*(*pages)[1].GetDict().FindString("value"), "subject");
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
  const base::DictValue* questions = parsed->FindDict("questions");
  ASSERT_TRUE(questions);
  EXPECT_EQ(questions->size(), 3u);
  const base::DictValue* destination = questions->FindDict("destination");
  ASSERT_TRUE(destination);
  EXPECT_EQ(*destination->FindString("type"), "choice");
  const base::DictValue* criteria = destination->FindDict("criteria");
  ASSERT_TRUE(criteria);
  EXPECT_EQ(criteria->size(), 2u);
  EXPECT_EQ(*criteria->FindString("task-1"), "Plan Lisbon trip");
  EXPECT_TRUE(criteria->FindString(kNoTaskOption));

  const base::DictValue* otherwise = questions->FindDict("otherwise");
  ASSERT_TRUE(otherwise);
  const base::DictValue* kinds = otherwise->FindDict("criteria");
  ASSERT_TRUE(kinds);
  EXPECT_EQ(kinds->size(), 2u);
  EXPECT_TRUE(kinds->FindString(kNewTaskOption));
  EXPECT_TRUE(kinds->FindString(kScratchpadOption));
}

// The request Jev is sent. The wording of its questions was measured against
// real requests, so a change to it should be measured too.
TEST_F(AiProtocolTest, SystemOneRequestIsTheOneJevAnswered) {
  const auto body = BuildDecisionRequestBody(
      DecisionFormat::kSystemOne, "jev-latest", LisbonContext(),
      {{"task-1", "Plan Lisbon trip"}, {"task-2", "Benchmark models"}});
  ASSERT_TRUE(body);
  const auto sent = base::JSONReader::ReadDict(
      R"({"model":"jev-latest","state":"Title: Lisbon in three days\nURL: h)"
      R"(ttps://example.com/lisbon\nSource: opened from \"Hacker News\"\nPa)"
      R"(ge excerpt: Day one: Alfama and the castle.","questions":{"destina)"
      R"(tion":{"type":"choice","instructions":"Decide which of the user's )"
      R"(browsing tasks this tab belongs to. Choose a task only when the ta)"
      R"(b clearly continues the work that task names. Choose none when it )"
      R"(fits none of them.","criteria":{"task-1":"Plan Lisbon trip","task-)"
      R"(2":"Benchmark models","none":"The tab belongs to none of these tas)"
      R"(ks."}},"otherwise":{"type":"choice","instructions":"Suppose this t)"
      R"(ab belongs to none of the user's existing browsing tasks. Decide w)"
      R"(hat it is.","criteria":{"new_task":"The tab starts a piece of work)"
      R"( or research of its own that the user is likely to continue in mor)"
      R"(e tabs.","scratchpad":"The tab is a one-off: a quick lookup, a sig)"
      R"(n-in or verification page, a home page, a feed, or a dashboard."}})"
      R"(,"kind":{"type":"choice","instructions":"Decide what kind of page )"
      R"(this tab shows.","criteria":{"hub":"The tab is a place the user co)"
      R"(mes back to for many things: a home page, a feed, an inbox, a dash)"
      R"(board, or a list of items.","subject":"The tab is about one subjec)"
      R"(t: an article, a search for something, a document, a product, a v)"
      R"(ideo, or a discussion."}}}})",
      base::JSON_PARSE_RFC);
  ASSERT_TRUE(sent);
  EXPECT_EQ(base::JSONReader::ReadDict(*body, base::JSON_PARSE_RFC), sent);
}

// With no task to belong to, the first question is not worth asking.
TEST_F(AiProtocolTest, DecisionRequestWithoutTasksDoesNotAskWhichTask) {
  auto body = BuildDecisionRequestBody(DecisionFormat::kSystemOne, "jev-latest",
                                       LisbonContext(), {});
  ASSERT_TRUE(body);
  auto parsed = base::JSONReader::ReadDict(*body, base::JSON_PARSE_RFC);
  ASSERT_TRUE(parsed);
  const base::DictValue* questions = parsed->FindDict("questions");
  ASSERT_TRUE(questions);
  EXPECT_EQ(questions->size(), 2u);
  EXPECT_TRUE(questions->FindDict("otherwise"));
  EXPECT_TRUE(questions->FindDict("kind"));

  body = BuildDecisionRequestBody(DecisionFormat::kDecisions, "gpt-6-luna",
                                  LisbonContext(), {});
  ASSERT_TRUE(body);
  parsed = base::JSONReader::ReadDict(*body, base::JSON_PARSE_RFC);
  ASSERT_TRUE(parsed);
  ASSERT_EQ(parsed->FindList("questions")->size(), 2u);
  EXPECT_EQ(*(*parsed->FindList("questions"))[0].GetDict().FindString("name"),
            "otherwise");
}

TEST_F(AiProtocolTest, DecisionRequestRejectsBadInput) {
  const BranchContext context = LisbonContext();
  for (DecisionFormat format :
       {DecisionFormat::kDecisions, DecisionFormat::kSystemOne}) {
    EXPECT_FALSE(BuildDecisionRequestBody(format, "", context, {}));
    EXPECT_FALSE(
        BuildDecisionRequestBody(format, "model", BranchContext(), {}));
    for (const char* reserved :
         {kScratchpadOption, kNewTaskOption, kNoTaskOption}) {
      EXPECT_FALSE(BuildDecisionRequestBody(format, "model", context,
                                            {{reserved, "x"}}));
    }
    std::vector<std::pair<std::string, std::string>> tasks;
    for (int i = 0; i < 254; ++i) {
      tasks.emplace_back("task-" + base::NumberToString(i), "Task");
    }
    EXPECT_TRUE(BuildDecisionRequestBody(format, "model", context, tasks));
    tasks.emplace_back("one-too-many", "Task");
    EXPECT_FALSE(BuildDecisionRequestBody(format, "model", context, tasks));
  }
}

TEST_F(AiProtocolTest, DecisionsResponseParsesBothAnswers) {
  auto answer = ParseDecisionResponse(DecisionFormat::kDecisions, R"({
    "model": "gpt-6-luna",
    "answers": [
      {"type": "predicate", "name": "other", "probability": 0.2},
      {"type": "choice", "name": "destination", "choice": "task-1",
       "confidence": 0.94,
       "probabilities": [{"value": "task-1", "probability": 0.94},
                         {"value": "none", "probability": 0.06}]}
    ]})",
                                      /*has_tasks=*/true);
  ASSERT_TRUE(answer);
  EXPECT_EQ(answer->choice, "task-1");
  EXPECT_DOUBLE_EQ(answer->confidence, 0.94);
  EXPECT_FALSE(answer->task_name);

  // Without a confidence field, the chosen value's probability stands in.
  answer = ParseDecisionResponse(DecisionFormat::kDecisions, R"({"answers": [
      {"type": "choice", "name": "destination", "choice": "none",
       "probabilities": [{"value": "none", "probability": 0.91}]},
      {"type": "choice", "name": "otherwise", "choice": "scratchpad",
       "probabilities": [{"value": "new_task", "probability": 0.2},
                         {"value": "scratchpad", "probability": 0.8}]}]})",
                                 /*has_tasks=*/true);
  ASSERT_TRUE(answer);
  EXPECT_EQ(answer->choice, kScratchpadOption);
  EXPECT_DOUBLE_EQ(answer->confidence, 0.91);
  EXPECT_DOUBLE_EQ(answer->new_task, 0.2);

  // Without probabilities, the confidence in the choice stands in.
  answer = ParseDecisionResponse(DecisionFormat::kDecisions, R"({"answers": [
      {"type": "choice", "name": "otherwise", "choice": "scratchpad",
       "confidence": 0.7}]})",
                                 /*has_tasks=*/false);
  ASSERT_TRUE(answer);
  EXPECT_EQ(answer->choice, kScratchpadOption);
  EXPECT_DOUBLE_EQ(answer->confidence, 1.0);
  EXPECT_NEAR(answer->new_task, 0.3, 1e-9);
}

TEST_F(AiProtocolTest, DecisionsResponseRejectsUnusableAnswers) {
  const auto parse = [](const char* body) {
    return ParseDecisionResponse(DecisionFormat::kDecisions, body,
                                 /*has_tasks=*/true);
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
  // Outside every task, without saying what the tab is.
  EXPECT_FALSE(parse(R"({"answers": [
      {"type": "choice", "name": "destination", "choice": "none", "confidence": 0.99}]})"));
  // The first question never offers the answers to the second.
  EXPECT_FALSE(parse(R"({"answers": [
      {"type": "choice", "name": "destination", "choice": "scratchpad", "confidence": 0.99},
      {"type": "choice", "name": "otherwise", "choice": "scratchpad", "confidence": 0.99}]})"));
  // An answer about tasks is required when tasks were listed.
  EXPECT_FALSE(parse(R"({"answers": [
      {"type": "choice", "name": "otherwise", "choice": "scratchpad", "confidence": 0.99}]})"));
  EXPECT_FALSE(parse(R"({"answers": [
      {"type": "choice", "name": "destination", "choice": "none", "confidence": 0.99},
      {"type": "choice", "name": "otherwise", "choice": "task-1", "confidence": 0.99}]})"));
}

TEST_F(AiProtocolTest, SystemOneResponseRejectsUnusableAnswers) {
  const auto parse = [](const char* body) {
    return ParseDecisionResponse(DecisionFormat::kSystemOne, body,
                                 /*has_tasks=*/true);
  };
  // A fallback language model reports no confidence as zero.
  const auto answer = parse(R"({"answers": {
      "destination": {"type": "choice", "choice": "task-1", "confidence": 0,
                      "probabilities": {}}}})");
  ASSERT_TRUE(answer);
  EXPECT_DOUBLE_EQ(answer->confidence, 0.0);

  EXPECT_FALSE(parse(R"({"answers": {}})"));
  EXPECT_FALSE(
      parse(R"({"answers": {"destination": {"type": "noul", "noul": 0.9}}})"));
  EXPECT_FALSE(parse(
      R"({"answers": {"destination": {"type": "choice", "choice": "task-1"}}})"));
  EXPECT_FALSE(parse(R"({"answers": {"otherwise": {
      "type": "choice", "choice": "scratchpad", "confidence": 0.98,
      "probabilities": {"new_task": 0.01, "scratchpad": 0.99}}}})"));
  EXPECT_FALSE(parse(R"({"answers": {
      "destination": {"type": "choice", "choice": "none", "confidence": 1},
      "otherwise": {"type": "choice", "choice": "scratchpad", "confidence": 0.9,
                    "probabilities": {"new_task": 7, "scratchpad": 0.99}}}})"));
}

// The wording of the name check was measured against Jev, so a change to it
// should be measured too.
TEST_F(AiProtocolTest, ANameCheckAsksWhetherTheTabIsTheSamePieceOfWork) {
  const std::vector<std::pair<std::string, std::string>> tasks = {
      {"task-1", "Compare Baixa hotels"}};
  std::vector<std::string> titles(kMaxNamingTitles + 1, "Chiado hotels");
  titles.back() = "One title too many";
  const NameCheck check{titles};

  auto parsed = base::JSONReader::ReadDict(
      *BuildDecisionRequestBody(DecisionFormat::kSystemOne, "jev-latest",
                                LisbonContext(), tasks, check),
      base::JSON_PARSE_RFC);
  ASSERT_TRUE(parsed);
  const base::DictValue* questions = parsed->FindDict("questions");
  ASSERT_TRUE(questions);
  EXPECT_EQ(questions->size(), 4u);
  const base::DictValue* name = questions->FindDict("name");
  ASSERT_TRUE(name);
  EXPECT_EQ(*name->FindString("type"), "choice");
  const std::string instructions = *name->FindString("instructions");
  EXPECT_TRUE(instructions.starts_with(
      "The tab sits in a group with these tabs:\n- Chiado hotels\n"));
  EXPECT_TRUE(instructions.ends_with(
      "- Chiado hotels\nDecide whether this tab is part of the same piece of "
      "work as those tabs."));
  // The task's name is the business of the first question alone.
  EXPECT_EQ(instructions.find("Baixa"), std::string::npos);
  EXPECT_EQ(instructions.find("One title too many"), std::string::npos);
  const base::DictValue* criteria = name->FindDict("criteria");
  ASSERT_TRUE(criteria);
  EXPECT_EQ(criteria->size(), 2u);
  EXPECT_EQ(*criteria->FindString("same"),
            "This tab continues the piece of work those tabs share.");
  EXPECT_EQ(*criteria->FindString("different"),
            "This tab is a different piece of work from those tabs.");
  // The tab's own description does not say which task it is in.
  EXPECT_EQ(parsed->FindString("state")->find("Baixa"), std::string::npos);

  parsed = base::JSONReader::ReadDict(
      *BuildDecisionRequestBody(DecisionFormat::kDecisions, "gpt-6-luna",
                                LisbonContext(), tasks, check),
      base::JSON_PARSE_RFC);
  ASSERT_TRUE(parsed);
  const base::ListValue* listed = parsed->FindList("questions");
  ASSERT_TRUE(listed);
  ASSERT_EQ(listed->size(), 4u);
  const base::DictValue& last = (*listed)[2].GetDict();
  EXPECT_EQ(*last.FindString("name"), "name");
  EXPECT_EQ(*last.FindString("instructions"), instructions);
  const base::ListValue* choices = last.FindList("choices");
  ASSERT_TRUE(choices);
  ASSERT_EQ(choices->size(), 2u);
  EXPECT_EQ(*(*choices)[0].GetDict().FindString("value"), "same");
  EXPECT_EQ(*(*choices)[1].GetDict().FindString("value"), "different");

  // A chat request never carries it.
  EXPECT_EQ(BuildDecisionRequestBody(DecisionFormat::kChat, "model",
                                     LisbonContext(), tasks, check),
            BuildDecisionRequestBody(DecisionFormat::kChat, "model",
                                     LisbonContext(), tasks));
}

TEST_F(AiProtocolTest, TheNameCheckAnswerIsTheProbabilityOfTheSameWork) {
  const auto system_one = [](const std::string& name, bool asked = true) {
    const auto answer = ParseDecisionResponse(
        DecisionFormat::kSystemOne,
        R"({"answers": {"destination": {"type": "choice", "choice": "none",
            "confidence": 0.97}, "otherwise": {"type": "choice",
            "choice": "new_task", "confidence": 0.8})" +
            name + "}}",
        /*has_tasks=*/true, asked);
    EXPECT_TRUE(answer);
    // The placement is read the same with or without the name check.
    EXPECT_EQ(answer->choice, kNewTaskOption);
    EXPECT_DOUBLE_EQ(answer->confidence, 0.97);
    return answer->same_work;
  };
  EXPECT_DOUBLE_EQ(system_one(""), 0.0);
  EXPECT_DOUBLE_EQ(system_one(R"(, "name": {"type": "choice", "choice": "same",
          "confidence": 0.7,
          "probabilities": {"different": 0.17, "same": 0.83}})"),
                   0.83);
  EXPECT_DOUBLE_EQ(
      system_one(R"(, "name": {"type": "choice", "choice": "different",
          "confidence": 0.9,
          "probabilities": {"different": 0.94, "same": 0.06}})"),
      0.06);
  // Without probabilities, the confidence in the choice stands in.
  EXPECT_NEAR(system_one(R"(, "name": {"type": "choice", "choice": "different",
                  "confidence": 0.9})"),
              0.1, 1e-9);
  // An answer to a question the request did not ask renames nothing.
  EXPECT_DOUBLE_EQ(system_one(R"(, "name": {"type": "choice", "choice": "same",
          "confidence": 0.99})",
                              /*asked=*/false),
                   0.0);
  // An answer uw cannot use renames nothing.
  EXPECT_DOUBLE_EQ(
      system_one(R"(, "name": {"type": "choice", "choice": "perhaps",
          "confidence": 0.99})"),
      0.0);
  EXPECT_DOUBLE_EQ(system_one(R"(, "name": {"type": "noul", "noul": 0.9})"),
                   0.0);
  EXPECT_DOUBLE_EQ(system_one(R"(, "name": {"type": "choice", "choice": "same",
          "confidence": 0.9, "probabilities": {"different": 0.1, "same": 7}})"),
                   0.0);

  const auto decisions =
      ParseDecisionResponse(DecisionFormat::kDecisions, R"({"answers": [
      {"type": "choice", "name": "destination", "choice": "task-1",
       "confidence": 0.8},
      {"type": "choice", "name": "name", "choice": "same",
       "probabilities": [{"value": "different", "probability": 0.25},
                         {"value": "same", "probability": 0.75}]}]})",
                            /*has_tasks=*/true, /*has_name_check=*/true);
  ASSERT_TRUE(decisions);
  EXPECT_EQ(decisions->choice, "task-1");
  EXPECT_DOUBLE_EQ(decisions->same_work, 0.75);
}

// A hub is a page the user comes back to for many things. The wording was
// measured against Jev on the real pages of the organizer log.
TEST_F(AiProtocolTest, TheKindAnswerIsTheProbabilityOfAHub) {
  const auto parsed = base::JSONReader::ReadDict(
      *BuildDecisionRequestBody(DecisionFormat::kSystemOne, "jev-latest",
                                LisbonContext(), {}),
      base::JSON_PARSE_RFC);
  ASSERT_TRUE(parsed);
  const base::DictValue* kind = parsed->FindDictByDottedPath("questions.kind");
  ASSERT_TRUE(kind);
  EXPECT_EQ(*kind->FindString("instructions"),
            "Decide what kind of page this tab shows.");
  EXPECT_EQ(*kind->FindStringByDottedPath("criteria.hub"),
            "The tab is a place the user comes back to for many things: a "
            "home page, a feed, an inbox, a dashboard, or a list of items.");
  EXPECT_EQ(*kind->FindStringByDottedPath("criteria.subject"),
            "The tab is about one subject: an article, a search for "
            "something, a document, a product, a video, or a discussion.");

  const auto hub = [](const std::string& kind_answer) {
    const auto answer = ParseDecisionResponse(
        DecisionFormat::kSystemOne,
        R"({"answers": {"otherwise": {"type": "choice", "choice": "new_task",
            "confidence": 0.8})" +
            kind_answer + "}}",
        /*has_tasks=*/false);
    EXPECT_TRUE(answer);
    // The placement is read the same whatever the page is.
    EXPECT_EQ(answer->choice, kNewTaskOption);
    return answer->hub;
  };
  EXPECT_DOUBLE_EQ(hub(""), 0.0);
  EXPECT_DOUBLE_EQ(hub(R"(, "kind": {"type": "choice", "choice": "hub",
      "confidence": 0.9, "probabilities": {"hub": 0.97, "subject": 0.03}})"),
                   0.97);
  EXPECT_DOUBLE_EQ(hub(R"(, "kind": {"type": "choice", "choice": "subject",
      "confidence": 0.9, "probabilities": {"hub": 0.02, "subject": 0.98}})"),
                   0.02);
  // An answer uw cannot use holds no page back.
  EXPECT_DOUBLE_EQ(hub(R"(, "kind": {"type": "choice", "choice": "portal",
      "confidence": 0.99})"),
                   0.0);

  const auto decisions =
      ParseDecisionResponse(DecisionFormat::kDecisions, R"({"answers": [
      {"type": "choice", "name": "otherwise", "choice": "scratchpad",
       "confidence": 0.9},
      {"type": "choice", "name": "kind", "choice": "hub",
       "probabilities": [{"value": "hub", "probability": 0.75},
                         {"value": "subject", "probability": 0.25}]}]})",
                            /*has_tasks=*/false);
  ASSERT_TRUE(decisions);
  EXPECT_DOUBLE_EQ(decisions->hub, 0.75);

  // A chat request never carries the question.
  EXPECT_EQ(BuildDecisionRequestBody(DecisionFormat::kChat, "model",
                                     LisbonContext(), {})
                ->find("\"kind\""),
            std::string::npos);
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
          R"({"task": "none", "confidence": 0.95, "new_task": 0.9, "name": "Plan trip"})"),
      /*has_tasks=*/true);
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

TEST_F(AiProtocolTest, NamingATaskListsItsOtherTabs) {
  const auto messages_of = [](const std::string& body) {
    return base::JSONReader::ReadDict(body, base::JSON_PARSE_RFC)
        ->FindList("messages")
        ->Clone();
  };
  const base::ListValue alone =
      messages_of(*BuildNamingRequestBody("model", LisbonContext()));
  EXPECT_EQ(alone[1].GetDict().FindString("content")->find("Other tabs"),
            std::string::npos);

  std::vector<std::string> titles(kMaxNamingTitles + 1, "Alfama walking tour");
  titles.back() = "One title too many";
  const base::ListValue with_others =
      messages_of(*BuildNamingRequestBody("model", LisbonContext(), titles));
  const std::string& tabs = *with_others[1].GetDict().FindString("content");
  EXPECT_NE(tabs.find("Title: Lisbon in three days"), std::string::npos);
  EXPECT_NE(tabs.find("\nOther tabs in the task:\n- Alfama walking tour\n"),
            std::string::npos);
  EXPECT_EQ(tabs.find("One title too many"), std::string::npos);
  // A task with several tabs is not one that a single tab starts.
  EXPECT_NE(*alone[0].GetDict().FindString("content"),
            *with_others[0].GetDict().FindString("content"));
}

// deepseek-flash thinks before it answers. With a limit of 32 tokens it spent
// them all on thinking and answered with empty content.
TEST_F(AiProtocolTest, ChatRequestsLeaveRoomForAModelThatThinksFirst) {
  const BranchContext context = LisbonContext();
  for (const std::optional<std::string>& body :
       {BuildNamingRequestBody("deepseek-flash", context),
        BuildClassificationRequestBody("deepseek-flash", context, {})}) {
    ASSERT_TRUE(body);
    auto parsed = base::JSONReader::ReadDict(*body, base::JSON_PARSE_RFC);
    ASSERT_TRUE(parsed);
    EXPECT_GE(parsed->FindInt("max_tokens").value_or(0), 2048);
  }

  EXPECT_EQ(ParseNamingResponse(
                R"({"choices":[{"finish_reason":"stop","message":{
              "role":"assistant","content":"Plan Lisbon trip",
              "reasoning_content":"The tab is a Lisbon travel guide."}}]})"),
            u"Plan Lisbon trip");
  EXPECT_FALSE(ParseNamingResponse(
      R"({"choices":[{"finish_reason":"length","message":{
          "role":"assistant","content":"",
          "reasoning_content":"The tab is a Lisbon travel"}}]})"));
}

}  // namespace
}  // namespace uw
