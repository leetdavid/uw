// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/components/agent/agent_protocol.h"

#include <optional>
#include <string>
#include <vector>

#include "base/json/json_reader.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace uw {
namespace {

TEST(AgentProtocolTest, ARequestCarriesTheConversationAndTheTools) {
  base::ListValue messages;
  messages.Append(AgentInstructions());
  messages.Append(UserMessage("Is the Vercel deploy green?"));
  base::ListValue tools;
  tools.Append(base::DictValue().Set("type", "function"));

  const std::optional<std::string> body =
      BuildAgentRequestBody("claude-sonnet-5-5", messages, tools);

  ASSERT_TRUE(body);
  std::optional<base::DictValue> request =
      base::JSONReader::ReadDict(*body, base::JSON_PARSE_RFC);
  ASSERT_TRUE(request);
  EXPECT_EQ(*request->FindString("model"), "claude-sonnet-5-5");
  const base::ListValue& sent = *request->FindList("messages");
  ASSERT_EQ(sent.size(), 2u);
  EXPECT_EQ(*sent[0].GetDict().FindString("role"), "system");
  EXPECT_EQ(*sent[1].GetDict().FindString("content"),
            "Is the Vercel deploy green?");
  EXPECT_EQ(request->FindList("tools")->size(), 1u);
  EXPECT_FALSE(BuildAgentRequestBody("", messages, tools));
}

TEST(AgentProtocolTest, DescribesEveryTool) {
  const base::ListValue tools = AgentToolSchemas();

  std::vector<std::string> names;
  for (const base::Value& tool : tools) {
    const base::DictValue& function = *tool.GetDict().FindDict("function");
    names.push_back(*function.FindString("name"));
    EXPECT_FALSE(function.FindString("description")->empty());
    EXPECT_EQ(*function.FindDict("parameters")->FindString("type"), "object");
  }
  EXPECT_EQ(names, (std::vector<std::string>{"open", "navigate", "read",
                                             "click", "type"}));
}

TEST(AgentProtocolTest, ReadsToolCalls) {
  const std::optional<ModelTurn> turn = ParseAgentResponse(R"({"choices":[
    {"message":{"role":"assistant","content":null,"tool_calls":[
      {"id":"call_1","type":"function",
       "function":{"name":"open","arguments":"{\"url\":\"https://vercel.com\"}"}},
      {"id":"call_2","type":"function",
       "function":{"name":"read","arguments":"{\"tab\":1}"}}]},
     "finish_reason":"tool_calls"}]})");

  ASSERT_TRUE(turn);
  EXPECT_EQ(turn->text, "");
  ASSERT_EQ(turn->calls.size(), 2u);
  EXPECT_EQ(turn->calls[0].id, "call_1");
  EXPECT_EQ(turn->calls[0].name, "open");
  EXPECT_EQ(turn->calls[0].arguments, R"({"url":"https://vercel.com"})");
  EXPECT_EQ(turn->calls[1].name, "read");
}

TEST(AgentProtocolTest, ReadsAnAnswer) {
  const std::optional<ModelTurn> turn = ParseAgentResponse(
      R"({"choices":[{"message":{"role":"assistant",
          "content":"The deploy passed."},"finish_reason":"stop"}]})");

  ASSERT_TRUE(turn);
  EXPECT_EQ(turn->text, "The deploy passed.");
  EXPECT_TRUE(turn->calls.empty());
}

TEST(AgentProtocolTest, RefusesWhatIsNotAnAnswer) {
  EXPECT_FALSE(ParseAgentResponse("not json"));
  EXPECT_FALSE(ParseAgentResponse(R"({"choices":[]})"));
  EXPECT_FALSE(ParseAgentResponse(
      R"({"choices":[{"message":{"role":"assistant","content":""}}]})"));
  EXPECT_FALSE(ParseAgentResponse(R"({"choices":[{"message":{
      "tool_calls":[{"type":"function","function":{"name":"open"}}]}}]})"));
}

TEST(AgentProtocolTest, TheModelHearsBackWhatItAskedFor) {
  const ModelTurn turn{"Opening it.", {{"call_1", "open", R"({"url":"x"})"}}};

  const base::DictValue asked = AssistantMessage(turn);
  const base::DictValue answered =
      ToolMessage("call_1", base::DictValue().Set("tab", 1));

  EXPECT_EQ(*asked.FindString("content"), "Opening it.");
  const base::DictValue& call = (*asked.FindList("tool_calls"))[0].GetDict();
  EXPECT_EQ(*call.FindString("id"), "call_1");
  EXPECT_EQ(*call.FindDict("function")->FindString("arguments"),
            R"({"url":"x"})");
  EXPECT_EQ(*answered.FindString("role"), "tool");
  EXPECT_EQ(*answered.FindString("tool_call_id"), "call_1");
  EXPECT_EQ(*answered.FindString("content"), R"({"tab":1})");
}

}  // namespace
}  // namespace uw
