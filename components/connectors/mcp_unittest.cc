// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/components/connectors/mcp.h"

#include "base/json/json_reader.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace uw::mcp {
namespace {

base::DictValue Json(std::string_view text) {
  return base::JSONReader::ReadDict(text, base::JSON_PARSE_RFC).value();
}

TEST(McpTest, AModernRequestCarriesTheVersion) {
  const base::DictValue modern = Json(RequestBody(
      7, Era::kModern, "tools/call", base::DictValue().Set("name", "x")));
  EXPECT_EQ(*modern.FindString("jsonrpc"), "2.0");
  EXPECT_EQ(*modern.FindInt("id"), 7);
  EXPECT_EQ(*modern.FindString("method"), "tools/call");
  const base::DictValue* meta = modern.FindDictByDottedPath("params._meta");
  ASSERT_TRUE(meta);
  EXPECT_EQ(*meta->FindString("io.modelcontextprotocol/protocolVersion"),
            "2026-07-28");
  EXPECT_EQ(
      *meta->FindDict("io.modelcontextprotocol/clientInfo")->FindString("name"),
      "uw");

  const base::DictValue legacy =
      Json(RequestBody(8, Era::kLegacy, "tools/list"));
  EXPECT_FALSE(legacy.FindDictByDottedPath("params._meta"));
  EXPECT_EQ(*Json(InitializedNotification()).FindString("method"),
            "notifications/initialized");
  EXPECT_EQ(*InitializeParams().FindString("protocolVersion"), "2025-11-25");
}

TEST(McpTest, ReadsResponsesErrorsAndServerRequests) {
  std::optional<Message> result =
      ParseMessage(R"({"jsonrpc":"2.0","id":3,"result":{"tools":[]}})");
  ASSERT_TRUE(result);
  EXPECT_EQ(result->id, 3);
  EXPECT_FALSE(result->error);
  EXPECT_TRUE(result->result.FindList("tools"));

  std::optional<Message> error = ParseMessage(
      R"({"jsonrpc":"2.0","id":1,"error":{"code":-32022,"message":"no",
          "data":{"supported":["2027-01-01"],"requested":"2026-07-28"}}})");
  ASSERT_TRUE(error && error->error);
  EXPECT_TRUE(IsModernError(*error->error));
  EXPECT_EQ(error->error->supported, std::vector<std::string>{"2027-01-01"});

  std::optional<Message> unknown = ParseMessage(
      R"({"jsonrpc":"2.0","id":1,"error":{"code":-32601,"message":"?"}})");
  ASSERT_TRUE(unknown && unknown->error);
  EXPECT_FALSE(IsModernError(*unknown->error));

  std::optional<Message> ping =
      ParseMessage(R"({"jsonrpc":"2.0","id":"s1","method":"ping"})");
  ASSERT_TRUE(ping);
  EXPECT_EQ(ping->method, "ping");
  EXPECT_FALSE(ping->id);
  EXPECT_TRUE(
      Json(AnswerToServer(ping->request_id, "ping")).FindDict("result"));
  EXPECT_EQ(*Json(AnswerToServer(ping->request_id, "roots/list"))
                 .FindIntByDottedPath("error.code"),
            -32601);

  EXPECT_FALSE(ParseMessage("not json"));
}

TEST(McpTest, ReadsAnEventStream) {
  const std::vector<Message> messages = ParseEventStream(
      ": keep-alive\r\n"
      "event: message\r\n"
      "data: {\"jsonrpc\":\"2.0\",\"method\":\"notifications/progress\"}\r\n"
      "\r\n"
      "data: {\"jsonrpc\":\"2.0\",\n"
      "data: \"id\":4,\"result\":{}}\n\n");
  ASSERT_EQ(messages.size(), 2u);
  EXPECT_EQ(messages[0].method, "notifications/progress");
  EXPECT_EQ(messages[1].id, 4);
}

TEST(McpTest, ReadsToolsAndPrompts) {
  const base::DictValue page = Json(R"({
    "tools": [
      {"name": "list_issues", "description": "Lists issues.",
       "inputSchema": {"type": "object", "properties": {}},
       "annotations": {"readOnlyHint": true}},
      {"name": "save_issue", "description": "Saves one."},
      {"description": "no name"}
    ],
    "nextCursor": "page-2"
  })");
  const std::vector<Tool> tools = ParseTools(page);
  ASSERT_EQ(tools.size(), 2u);
  EXPECT_TRUE(tools[0].read_only);
  EXPECT_FALSE(tools[1].read_only);
  EXPECT_EQ(*tools[1].input_schema.FindString("type"), "object");
  EXPECT_EQ(NextCursor(page), "page-2");
  EXPECT_FALSE(NextCursor(Json(R"({"tools": []})")));
  EXPECT_TRUE(ToolFromValue(ToolToValue(tools[0]))->read_only);

  const std::vector<Prompt> prompts = ParsePrompts(Json(R"({"prompts": [
    {"name": "triage_issue", "description": "Triage an issue.",
     "arguments": [{"name": "issue_id", "required": true},
                   {"name": "focus", "description": "What first"}]}]})"));
  ASSERT_EQ(prompts.size(), 1u);
  ASSERT_EQ(prompts[0].arguments.size(), 2u);
  EXPECT_TRUE(prompts[0].arguments[0].required);
  EXPECT_EQ(prompts[0].arguments[1].description, "What first");
  EXPECT_EQ(PromptFromValue(PromptToValue(prompts[0]))->arguments.size(), 2u);
}

TEST(McpTest, TurnsResultsIntoTextAndImages) {
  const Content content = ParseToolResult(Json(R"({"content": [
    {"type": "text", "text": "First"},
    {"type": "image", "data": "iVBOR", "mimeType": "image/png"},
    {"type": "resource_link", "uri": "file:///a.txt", "name": "a.txt"},
    {"type": "resource", "resource": {"uri": "file:///b", "text": "B"}}
  ]})"));
  EXPECT_EQ(content.text,
            "First\n\nLink: a.txt <file:///a.txt>\n\nfile:///b:\nB");
  EXPECT_EQ(content.images,
            std::vector<std::string>{"data:image/png;base64,iVBOR"});
  EXPECT_FALSE(content.failed);

  const Content structured = ParseToolResult(
      Json(R"({"structuredContent": {"n": 2}, "isError": true})"));
  EXPECT_EQ(structured.text, R"({"n":2})");
  EXPECT_TRUE(structured.failed);

  EXPECT_TRUE(
      ParseToolResult(Json(R"({"resultType": "input_required"})")).failed);

  EXPECT_EQ(ParsePromptResult(Json(R"({"messages": [
    {"role": "user", "content": {"type": "text", "text": "Triage WEB-3QX"}}
  ]})"))
                .text,
            "Triage WEB-3QX");
}

}  // namespace
}  // namespace uw::mcp
