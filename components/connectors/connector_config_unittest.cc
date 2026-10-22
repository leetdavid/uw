// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/components/connectors/connector_config.h"

#include <map>

#include "testing/gtest/include/gtest/gtest.h"

namespace uw {
namespace {

TEST(ConnectorConfigTest, ReadsWhatAReadmeGives) {
  auto connectors = ParseConnectors(R"({
    "mcpServers": {
      "brave-search": {
        "command": "npx",
        "args": ["-y", "@modelcontextprotocol/server-brave-search"],
        "env": {"BRAVE_API_KEY": "BSA123"}
      },
      "sentry": {"type": "http", "url": "https://mcp.sentry.dev/mcp",
                 "headers": {"Authorization": "Bearer abc"}}
    }
  })");
  ASSERT_TRUE(connectors.has_value());
  ASSERT_EQ(connectors->size(), 2u);
  const ConnectorConfig& brave = (*connectors)[0];
  EXPECT_EQ(brave.name, "brave-search");
  EXPECT_FALSE(brave.remote());
  EXPECT_EQ(brave.command, "npx");
  EXPECT_EQ(brave.args,
            (std::vector<std::string>{
                "-y", "@modelcontextprotocol/server-brave-search"}));
  EXPECT_EQ(brave.env.at("BRAVE_API_KEY"), "BSA123");
  const ConnectorConfig& sentry = (*connectors)[1];
  EXPECT_TRUE(sentry.remote());
  EXPECT_EQ(sentry.url, "https://mcp.sentry.dev/mcp");
  EXPECT_EQ(sentry.headers.at("Authorization"), "Bearer abc");
}

TEST(ConnectorConfigTest, ReadsTheInnerObjectAndOtherAgentsShapes) {
  auto bare = ParseConnectors(
      R"({"doop": {"url": "https://doop.design/mcp"}, // comment
          "fs": {"command": "npx", "args": ["x"],},})");
  ASSERT_TRUE(bare.has_value());
  ASSERT_EQ(bare->size(), 2u);
  EXPECT_TRUE((*bare)[0].remote());
  EXPECT_FALSE((*bare)[1].remote());

  auto vscode = ParseConnectors(
      R"({"servers": {"git": {"type": "stdio", "command": "uvx"}}})");
  ASSERT_TRUE(vscode.has_value());
  EXPECT_EQ((*vscode)[0].command, "uvx");
}

TEST(ConnectorConfigTest, SaysWhatIsWrong) {
  EXPECT_EQ(ParseConnectors("{\n\"a\": {\n\"command\": \"x\"\n\"b\"").error(),
            "That isn't JSON: the problem is at line 4.");
  EXPECT_EQ(ParseConnectors(R"({"command": "npx"})").error(),
            "Give the connector a name: {\"name\": {\"command\": ...}}.");
  EXPECT_EQ(ParseConnectors(R"({"mcpServers": {}})").error(),
            "There's no connector in it.");
  EXPECT_EQ(
      ParseConnectors(
          R"({"old": {"type": "sse", "url": "https://a.dev/events"}})")
          .error(),
      "old: uw doesn't speak the old HTTP+SSE transport. Use the server's "
      "Streamable HTTP address.");
  EXPECT_EQ(ParseConnectors(R"({"x": {"args": ["a"]}})").error(),
            "x: a connector on this Mac needs a \"command\".");
  EXPECT_EQ(ParseConnectors(R"({"x": {"url": "http://mcp.example.com/mcp"}})")
                .error(),
            "x: the address must be https, or http on this Mac or a private "
            "network.");
  EXPECT_EQ(ParseConnectors(R"({"my server": {"command": "a"}})").error(),
            "my server: name it with letters, digits, hyphens, and "
            "underscores.");
  EXPECT_EQ(
      ParseConnectors(R"({"x": {"command": "a", "env": {"K": 1}}})").error(),
      "x: each value in \"env\" must be a string.");
}

TEST(ConnectorConfigTest, McpRemoteBecomesTheWebConnectorItPassesOn) {
  auto connectors = ParseConnectors(R"({"mcpServers": {
    "linear": {"command": "npx",
               "args": ["-y", "mcp-remote", "https://mcp.linear.app/mcp"]},
    "posthog": {
      "command": "/opt/homebrew/bin/npx",
      "args": ["mcp-remote@latest", "https://mcp.posthog.com/mcp", "3334",
               "--header", "Authorization:${AUTH}", "--transport",
               "http-only", "--header=X-Team: 7"],
      "env": {"AUTH": "Bearer phx_1"}
    },
    "pnpm": {"command": "pnpm",
             "args": ["dlx", "mcp-remote", "https://a.dev/mcp"]},
    "installed": {"command": "mcp-remote", "args": ["https://b.dev/mcp"]}
  }})");
  ASSERT_TRUE(connectors.has_value()) << connectors.error();
  std::map<std::string, ConnectorConfig> by_name;
  for (const ConnectorConfig& connector : *connectors) {
    EXPECT_TRUE(connector.remote()) << connector.name;
    EXPECT_TRUE(connector.env.empty());
    by_name[connector.name] = connector;
  }
  ASSERT_EQ(by_name.size(), 4u);
  EXPECT_EQ(by_name["linear"].url, "https://mcp.linear.app/mcp");
  EXPECT_TRUE(by_name["linear"].headers.empty());
  EXPECT_EQ(by_name["posthog"].url, "https://mcp.posthog.com/mcp");
  EXPECT_EQ(by_name["posthog"].headers,
            (std::map<std::string, std::string>{
                {"Authorization", "Bearer phx_1"}, {"X-Team", "7"}}));
  EXPECT_EQ(by_name["pnpm"].url, "https://a.dev/mcp");
  EXPECT_EQ(by_name["installed"].url, "https://b.dev/mcp");
}

TEST(ConnectorConfigTest, OtherProgramsStayOnThisMac) {
  ConnectorConfig local;
  local.name = "fs";
  local.command = "npx";
  local.args = {"-y", "@modelcontextprotocol/server-filesystem", "/tmp"};
  EXPECT_FALSE(FromMcpRemote(local));
  local.args = {"-y", "mcp-remote"};
  EXPECT_FALSE(FromMcpRemote(local));
  local.command = "node";
  local.args = {"mcp-remote", "https://a.dev/mcp"};
  EXPECT_FALSE(FromMcpRemote(local));
}

TEST(ConnectorConfigTest, AnSseAddressIsTriedAtMcp) {
  EXPECT_EQ(StreamableAddress("https://mcp.posthog.com/sse"),
            "https://mcp.posthog.com/mcp");
  EXPECT_EQ(StreamableAddress("https://a.dev/v1/sse?team=7"),
            "https://a.dev/v1/mcp?team=7");
  EXPECT_FALSE(StreamableAddress("https://a.dev/mcp"));
  EXPECT_FALSE(StreamableAddress("https://a.dev/sse/x"));

  auto old = ParseConnectors(
      R"({"old": {"type": "sse", "url": "https://mcp.linear.app/sse"}})");
  ASSERT_TRUE(old.has_value());
  EXPECT_TRUE((*old)[0].remote());
  EXPECT_EQ((*old)[0].url, "https://mcp.linear.app/sse");
}

TEST(ConnectorConfigTest, KeepsSecretsOutOfTheStoredValue) {
  ConnectorConfig connector;
  connector.name = "brave";
  connector.command = "npx";
  connector.args = {"-y", "server"};
  connector.env = {{"BRAVE_API_KEY", "secret"}};

  const base::DictValue value = ConnectorToValue(connector);
  EXPECT_EQ(value.DebugString().find("secret"), std::string::npos);
  std::optional<ConnectorConfig> back = ConnectorFromValue(value);
  ASSERT_TRUE(back);
  EXPECT_EQ(back->args, connector.args);
  EXPECT_EQ(back->env.at("BRAVE_API_KEY"), "");

  SecretsFromJson(SecretsToJson(connector), *back);
  EXPECT_EQ(back->env.at("BRAVE_API_KEY"), "secret");
}

TEST(ConnectorConfigTest, NamesAreShort) {
  EXPECT_TRUE(IsConnectorName("linear-bilby_2"));
  EXPECT_FALSE(IsConnectorName(""));
  EXPECT_FALSE(IsConnectorName("a.b"));
  EXPECT_FALSE(IsConnectorName(std::string(65, 'a')));
}

}  // namespace
}  // namespace uw
