// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/components/agent/command_check.h"

#include <optional>
#include <string>

#include "base/json/json_reader.h"
#include "base/strings/strcat.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace uw {
namespace {

using ::testing::HasSubstr;
using ::testing::Not;

CommandCheckInput Grep() {
  CommandCheckInput input;
  input.chat = {"User: Which Stripe key does the shop use?",
                R"(Agent: run {"command":"ls -a"})"};
  input.step.tab = 1;
  input.step.folder = "~/code/shop";
  input.step.command = "grep -n STRIPE .env.local | head";
  return input;
}

base::DictValue Body(DecisionFormat format, const CommandCheckInput& input) {
  const std::optional<std::string> body =
      BuildCommandCheckRequestBody(format, "check-model", input);
  EXPECT_TRUE(body);
  std::optional<base::DictValue> dict =
      base::JSONReader::ReadDict(body.value_or("{}"), base::JSON_PARSE_RFC);
  EXPECT_TRUE(dict);
  return dict ? std::move(*dict) : base::DictValue();
}

TEST(CommandCheckTest, ADecisionModelGetsTheStepAndTwoQuestions) {
  const base::DictValue body = Body(DecisionFormat::kDecisions, Grep());

  EXPECT_EQ(*body.FindString("model"), "check-model");
  EXPECT_EQ(*body.FindString("input"),
            "What the user asked and what the agent did before, oldest "
            "first:\nUser: Which Stripe key does the shop use?\nAgent: run "
            "{\"command\":\"ls -a\"}\n\nThe step:\nRun in terminal tab 1, in "
            "~/code/shop:\ngrep -n STRIPE .env.local | head");
  const base::ListValue& questions = *body.FindList("questions");
  ASSERT_EQ(questions.size(), 2u);
  EXPECT_EQ(*questions[0].GetDict().FindString("name"), "danger");
  std::vector<std::string> dangers;
  for (const base::Value& choice :
       *questions[0].GetDict().FindList("choices")) {
    dangers.push_back(*choice.GetDict().FindString("value"));
  }
  EXPECT_THAT(dangers, testing::ElementsAre("safe", "deletes", "sends",
                                            "system", "secrets"));
  // Each word of the command that could name a file is a choice, and no
  // option is.
  EXPECT_EQ(*questions[1].GetDict().FindString("name"), "file");
  std::vector<std::string> files;
  for (const base::Value& choice :
       *questions[1].GetDict().FindList("choices")) {
    files.push_back(
        base::StrCat({*choice.GetDict().FindString("value"), "=",
                      *choice.GetDict().FindString("description")}));
  }
  EXPECT_EQ(files.size(), 6u);
  EXPECT_EQ(files[0], "w1=grep");
  EXPECT_EQ(files[1], "w2=STRIPE");
  EXPECT_EQ(files[2], "w3=.env.local");
  EXPECT_EQ(files[3], "w4=head");
  EXPECT_THAT(files[4], testing::StartsWith("other="));
  EXPECT_THAT(files[5], testing::StartsWith("none="));
}

TEST(CommandCheckTest, FullAutoAsksOnlyAboutSensitiveFiles) {
  CommandCheckInput input = Grep();
  input.file_only = true;

  const base::DictValue decisions = Body(DecisionFormat::kDecisions, input);
  ASSERT_EQ(decisions.FindList("questions")->size(), 1u);
  EXPECT_EQ(*(*decisions.FindList("questions"))[0].GetDict().FindString("name"),
            "file");

  const base::DictValue system_one = Body(DecisionFormat::kSystemOne, input);
  EXPECT_FALSE(system_one.FindDict("questions")->contains("danger"));
  EXPECT_EQ(*system_one.FindDictByDottedPath("questions.file.criteria")
                 ->FindString("w3"),
            ".env.local");

  const base::DictValue chat = Body(DecisionFormat::kChat, input);
  const std::string& instructions =
      *(*chat.FindList("messages"))[0].GetDict().FindString("content");
  EXPECT_THAT(instructions, HasSubstr("\"file_confidence\""));
  EXPECT_THAT(instructions, Not(HasSubstr("\"danger\"")));
}

TEST(CommandCheckTest, KeysAreDescribedForTheProgramTheyGoTo) {
  CommandCheckInput input;
  input.step.tab = 2;
  input.step.folder = "~";
  input.step.text = ":e .env";
  input.step.keys = {"Enter"};

  const base::DictValue body = Body(DecisionFormat::kSystemOne, input);

  EXPECT_EQ(*body.FindString("state"),
            "The step:\nKeys for the program running in terminal tab 2, in "
            "~:\ntext \":e .env\", then Enter");
  EXPECT_EQ(
      *body.FindDictByDottedPath("questions.file.criteria")->FindString("w2"),
      ".env");
}

TEST(CommandCheckTest, ALongChatKeepsItsFirstLineAndItsNewest) {
  CommandCheckInput input = Grep();
  input.chat.clear();
  input.chat.push_back("User: Find the Stripe key");
  for (int i = 0; i < 40; ++i) {
    input.chat.push_back(base::StrCat(
        {"Agent: run {\"command\":\"echo ", std::string(400, 'x'), "\"}"}));
  }
  input.chat.push_back("User: Only the test key");

  const std::string text =
      *Body(DecisionFormat::kDecisions, input).FindString("input");

  EXPECT_THAT(text, HasSubstr("oldest first:\nUser: Find the Stripe key\n["));
  EXPECT_THAT(text, HasSubstr(" lines left out]\nAgent: run"));
  EXPECT_THAT(text, HasSubstr("User: Only the test key\n\nThe step:"));
  EXPECT_LT(text.size(), 9000u);
}

TEST(CommandCheckTest, ReadsADecisionsAnswer) {
  const std::optional<CommandCheckAnswer> answer =
      ParseCommandCheckResponse(DecisionFormat::kDecisions,
                                R"({"answers":[
        {"type":"choice","name":"danger","choice":"safe","probabilities":[
          {"value":"safe","probability":0.55},
          {"value":"secrets","probability":0.3},
          {"value":"deletes","probability":0.15}]},
        {"type":"choice","name":"file","choice":"w3","probabilities":[
          {"value":"w3","probability":0.9},{"value":"none","probability":0.1}]}
      ]})",
                                Grep());

  ASSERT_TRUE(answer);
  EXPECT_DOUBLE_EQ(answer->safe, 0.55);
  EXPECT_EQ(answer->danger, "safe");
  EXPECT_DOUBLE_EQ(answer->sensitive, 0.9);
  EXPECT_EQ(answer->file, ".env.local");
}

TEST(CommandCheckTest, ReadsASystemOneAnswer) {
  const std::optional<CommandCheckAnswer> answer =
      ParseCommandCheckResponse(DecisionFormat::kSystemOne,
                                R"({"answers":{
        "danger":{"type":"choice","choice":"deletes","confidence":0.4,
                  "probabilities":{"safe":0.1,"deletes":0.7,"sends":0.2}},
        "file":{"type":"choice","choice":"none","confidence":0.9,
                "probabilities":{"none":0.95,"w1":0.05}}}})",
                                Grep());

  ASSERT_TRUE(answer);
  EXPECT_DOUBLE_EQ(answer->safe, 0.1);
  EXPECT_EQ(answer->danger, "deletes");
  EXPECT_NEAR(answer->sensitive, 0.05, 1e-9);
  EXPECT_EQ(answer->file, "grep");
}

TEST(CommandCheckTest, ReadsALanguageModelsAnswer) {
  const std::optional<CommandCheckAnswer> answer = ParseCommandCheckResponse(
      DecisionFormat::kChat,
      R"({"choices":[{"message":{"role":"assistant","content":
        "```json\n{\"danger\": \"secrets\", \"confidence\": 0.8, \"reason\": \"Prints the live Stripe key.\\nTo the chat.\", \"file\": \".env.local\", \"file_confidence\": 0.95}\n```"}}]})",
      Grep());

  ASSERT_TRUE(answer);
  EXPECT_NEAR(answer->safe, 0.2, 1e-9);
  EXPECT_EQ(answer->danger, "secrets");
  EXPECT_EQ(answer->reason, "Prints the live Stripe key. To the chat.");
  EXPECT_DOUBLE_EQ(answer->sensitive, 0.95);
  EXPECT_EQ(answer->file, ".env.local");
}

// A file the step does not name is not shown as one. One it names by its
// path is shown as the step writes it.
TEST(CommandCheckTest, ALanguageModelNamesOnlyAWordOfTheStep) {
  const auto read = [](std::string_view command, std::string_view file) {
    CommandCheckInput input;
    input.step.command = std::string(command);
    return ParseCommandCheckResponse(
        DecisionFormat::kChat,
        base::StrCat({R"({"choices":[{"message":{"content":"{\"danger\": )"
                      R"(\"safe\", \"confidence\": 0.9, \"file\": \")",
                      file, R"(\", \"file_confidence\": 0.6}"}}]})"}),
        input);
  };

  const std::optional<CommandCheckAnswer> elsewhere =
      read("grep STRIPE .env.local", "~/.aws/credentials");
  ASSERT_TRUE(elsewhere);
  EXPECT_DOUBLE_EQ(elsewhere->sensitive, 0.6);
  EXPECT_EQ(elsewhere->file, "");
  EXPECT_EQ(read("cat ~/.ssh/id_ed25519", "id_ed25519")->file,
            "~/.ssh/id_ed25519");
}

TEST(CommandCheckTest, AMalformedAnswerIsNoAnswer) {
  for (
      const char* body : {
          R"({"answers":[]})",
          R"({"answers":[{"name":"danger","choice":"maybe","confidence":0.9},
                          {"name":"file","choice":"none","confidence":0.9}]})",
          R"({"answers":[{"name":"danger","choice":"safe","confidence":0.9},
                          {"name":"file","choice":"none","confidence":1.5}]})",
          R"({"answers":[{"name":"danger","choice":"safe","confidence":0.9}]})",
          "not json",
      }) {
    EXPECT_FALSE(
        ParseCommandCheckResponse(DecisionFormat::kDecisions, body, Grep()))
        << body;
  }
  EXPECT_FALSE(ParseCommandCheckResponse(
      DecisionFormat::kChat,
      R"({"choices":[{"message":{"content":"{\"danger\": \"safe\"}"}}]})",
      Grep()));
}

TEST(CommandCheckTest, NoAnswerAsks) {
  const CommandCheckVerdict verdict =
      JudgeTerminalStep(std::nullopt, /*file_only=*/true, "deepseek-chat");

  EXPECT_TRUE(verdict.ask);
  EXPECT_FALSE(verdict.sensitive);
  EXPECT_EQ(verdict.reason, "The command check didn't answer.");
}

TEST(CommandCheckTest, ASensitiveFileAsksInEveryMode) {
  CommandCheckAnswer answer;
  answer.safe = 1.0;
  answer.danger = "safe";
  answer.sensitive = 0.5;
  answer.file = ".env.local";

  for (bool file_only : {false, true}) {
    const CommandCheckVerdict verdict =
        JudgeTerminalStep(answer, file_only, "deepseek-chat");
    EXPECT_TRUE(verdict.ask);
    EXPECT_TRUE(verdict.sensitive);
    EXPECT_EQ(verdict.file, ".env.local");
    EXPECT_EQ(verdict.reason,
              "A sensitive file. It can hold passwords and keys, and what the "
              "command prints goes to deepseek-chat.");
  }
}

TEST(CommandCheckTest, AutoAsksUnlessTheStepIsLikelySafe) {
  CommandCheckAnswer answer;
  answer.safe = 0.8;
  answer.danger = "safe";
  answer.sensitive = 0.49;
  EXPECT_FALSE(JudgeTerminalStep(answer, false, "m").ask);

  answer.safe = 0.79;
  CommandCheckVerdict verdict = JudgeTerminalStep(answer, false, "m");
  EXPECT_TRUE(verdict.ask);
  EXPECT_FALSE(verdict.sensitive);
  EXPECT_EQ(verdict.reason, "The command check wasn't sure it's safe.");
  // Full auto runs it.
  EXPECT_FALSE(JudgeTerminalStep(answer, true, "m").ask);

  answer.safe = 0.1;
  answer.danger = "deletes";
  EXPECT_EQ(JudgeTerminalStep(answer, false, "m").reason,
            "Deletes or overwrites files or data. What it removes may not "
            "come back.");
  answer.reason = "Removes every zip in Downloads.";
  EXPECT_EQ(JudgeTerminalStep(answer, false, "m").reason,
            "Removes every zip in Downloads.");
}

}  // namespace
}  // namespace uw
