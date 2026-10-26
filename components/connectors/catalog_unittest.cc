// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/components/connectors/catalog.h"

#include <map>
#include <string>
#include <utility>
#include <vector>

#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace uw {
namespace {

using ::testing::ElementsAre;

constexpr char kRaw[] = "https://raw.test/";

std::vector<std::string> Ids(const std::vector<CatalogEntry>& entries) {
  std::vector<std::string> ids;
  for (const CatalogEntry& entry : entries) {
    ids.push_back(entry.id);
  }
  return ids;
}

RegistryPage Page(std::string_view json) {
  std::optional<RegistryPage> page = ParseRegistryPage(json);
  EXPECT_TRUE(page);
  return page ? std::move(*page) : RegistryPage();
}

TEST(ConnectorCatalogTest, ReadsTheRegistrysWebServers) {
  RegistryPage page = Page(R"({
    "servers": [
      {"server": {"name": "app.linear/linear", "title": "Linear",
                  "description": "MCP server for Linear. Issues and more.",
                  "websiteUrl": "https://linear.app",
                  "remotes": [{"type": "sse", "url": "https://mcp.linear.app/sse"},
                              {"type": "streamable-http",
                               "url": "https://mcp.linear.app/mcp"}]},
       "_meta": {"io.modelcontextprotocol.registry/official":
                   {"status": "active", "isLatest": true}}},
      {"server": {"name": "ai.smithery/notion",
                  "remotes": [{"type": "streamable-http",
                               "url": "https://server.smithery.ai/notion/mcp",
                               "headers": [{"name": "Authorization",
                                            "description": "Smithery key",
                                            "isRequired": true,
                                            "isSecret": true,
                                            "value": "Bearer {smithery_key}"},
                                           {"name": "X-Region",
                                            "description": "Region",
                                            "default": "eu"}]}]}},
      {"server": {"name": "io.github.a/local-only",
                  "packages": [{"registryType": "npm"}]}},
      {"server": {"name": "io.github.a/gone",
                  "remotes": [{"type": "streamable-http",
                               "url": "https://gone.dev/mcp"}]},
       "_meta": {"io.modelcontextprotocol.registry/official":
                   {"status": "deleted", "isLatest": true}}}
    ],
    "metadata": {"nextCursor": "io.github.a/gone:1.0.0", "count": 4}
  })");
  ASSERT_EQ(page.entries.size(), 2u);
  const CatalogEntry& linear = page.entries[0];
  EXPECT_EQ(linear.id, "registry:app.linear/linear");
  EXPECT_EQ(linear.name, "linear");
  EXPECT_EQ(linear.title, "Linear");
  EXPECT_EQ(linear.description, "MCP server for Linear.");
  EXPECT_EQ(linear.url, "https://mcp.linear.app/mcp");
  EXPECT_TRUE(linear.fields.empty());

  const CatalogEntry& notion = page.entries[1];
  EXPECT_EQ(notion.title, "notion");
  EXPECT_EQ(notion.headers.at("Authorization"), "Bearer ${smithery_key}");
  EXPECT_EQ(notion.headers.at("X-Region"), "${X-Region}");
  ASSERT_EQ(notion.fields.size(), 2u);
  EXPECT_EQ(notion.fields[0].name, "smithery_key");
  EXPECT_TRUE(notion.fields[0].required);
  EXPECT_TRUE(notion.fields[0].secret);
  EXPECT_EQ(notion.fields[1].value, "eu");
  EXPECT_FALSE(notion.fields[1].required);

  EXPECT_THAT(page.removed,
              ElementsAre("registry:io.github.a/local-only",
                          "registry:io.github.a/gone"));
  EXPECT_EQ(page.next_cursor, "io.github.a/gone:1.0.0");
  EXPECT_FALSE(ParseRegistryPage("{}"));
}

TEST(ConnectorCatalogTest, FindsEachPluginsFolderOnGitHub) {
  std::vector<PluginRef> claude = ParseClaudeMarketplace(R"({"plugins": [
    {"name": "linear", "description": "Linear issue tracking.",
     "source": "./external_plugins/linear"},
    {"name": "posthog", "homepage": "https://posthog.com",
     "source": {"source": "url", "url": "https://github.com/PostHog/ai-plugin.git",
                "sha": "e0fbf5e742fb4242c4ea509b395b2e343642f0b7"}},
    {"name": "railway", "source": {"source": "git-subdir",
     "url": "https://github.com/railwayapp/railway-skills.git",
     "path": "plugins/railway", "ref": "main"}},
    {"name": "elsewhere", "source": {"source": "url",
     "url": "https://gitlab.com/a/b.git"}}
  ]})",
                                                         kRaw);
  ASSERT_EQ(claude.size(), 3u);
  EXPECT_EQ(claude[0].folder,
            "https://raw.test/anthropics/claude-plugins-official/main/"
            "external_plugins/linear/");
  EXPECT_EQ(claude[1].folder,
            "https://raw.test/PostHog/ai-plugin/"
            "e0fbf5e742fb4242c4ea509b395b2e343642f0b7/");
  EXPECT_EQ(claude[1].website, "https://posthog.com");
  EXPECT_EQ(claude[2].folder,
            "https://raw.test/railwayapp/railway-skills/main/plugins/railway/");
  EXPECT_FALSE(IsPinned(claude[0].folder));
  EXPECT_TRUE(IsPinned(claude[1].folder));

  std::vector<PluginRef> openai = ParseOpenAiMarketplace(R"({"plugins": [
    {"name": "granola", "source": {"source": "local", "path": "./plugins/granola"},
     "interface": {"displayName": "Granola"}}
  ]})",
                                                         kRaw);
  ASSERT_EQ(openai.size(), 1u);
  EXPECT_EQ(openai[0].folder, "https://raw.test/openai/plugins/main/plugins/granola/");
  EXPECT_EQ(openai[0].title, "Granola");
}

TEST(ConnectorCatalogTest, APluginsWebConnectorAndItsKeys) {
  PluginRef github{.source = CatalogSource::kClaudePlugin,
                   .name = "github",
                   .description = "Official GitHub MCP server.",
                   .folder = "https://raw.test/x/"};
  std::optional<CatalogEntry> entry = PluginEntry(github, R"({"github": {
    "type": "http", "url": "https://api.githubcopilot.com/mcp/",
    "headers": {"Authorization": "Bearer ${GITHUB_PERSONAL_ACCESS_TOKEN}",
                "X-Client": "uw"},
    "oauth": {"clientId": "someone-elses"}}})",
                                                  "");
  ASSERT_TRUE(entry);
  EXPECT_EQ(entry->id, "claude:github");
  EXPECT_EQ(entry->title, "github");
  ASSERT_EQ(entry->fields.size(), 1u);
  EXPECT_EQ(entry->fields[0].name, "GITHUB_PERSONAL_ACCESS_TOKEN");

  auto missing = ConnectorFor(*entry, "github", {});
  ASSERT_FALSE(missing.has_value());
  EXPECT_EQ(missing.error(), "Fill in GITHUB_PERSONAL_ACCESS_TOKEN.");
  auto config = ConnectorFor(*entry, "github",
                             {{"GITHUB_PERSONAL_ACCESS_TOKEN", "ghp_1"}});
  ASSERT_TRUE(config.has_value());
  EXPECT_EQ(config->url, "https://api.githubcopilot.com/mcp/");
  EXPECT_EQ(config->headers,
            (std::map<std::string, std::string>{
                {"Authorization", "Bearer ghp_1"}, {"X-Client", "uw"}}));

  PluginRef granola{.source = CatalogSource::kOpenAiPlugin,
                    .name = "granola",
                    .folder = "https://raw.test/y/"};
  entry = PluginEntry(
      granola,
      R"({"mcpServers": {"granola": {"type": "http",
                                     "url": "https://mcp.granola.ai/mcp"}}})",
      R"({"description": "Granola MCP connects your meetings.\n\nMore.",
          "homepage": "https://www.granola.ai",
          "interface": {"displayName": "Granola"}})");
  ASSERT_TRUE(entry);
  EXPECT_EQ(entry->id, "openai:granola");
  EXPECT_EQ(entry->title, "Granola");
  EXPECT_EQ(entry->description, "Granola MCP connects your meetings.");
  EXPECT_EQ(entry->website, "https://www.granola.ai");

  EXPECT_FALSE(PluginEntry(granola,
                           R"({"fs": {"command": "npx", "args": ["fs"]}})",
                           ""));
  EXPECT_TRUE(PluginEntry(
      github, "",
      R"({"mcpServers": {"a": {"type": "http", "url": "https://a.dev/mcp"}}})"));
  EXPECT_FALSE(PluginEntry(github, "", ""));
}

TEST(ConnectorCatalogTest, SearchShowsEachServerOnce) {
  ConnectorCatalog catalog;
  catalog.AddRegistryPage(Page(R"({"servers": [
    {"server": {"name": "app.linear/linear", "title": "Linear",
                "description": "Project management and issue tracking",
                "remotes": [{"type": "streamable-http",
                             "url": "https://mcp.linear.app/mcp"}]}},
    {"server": {"name": "io.pipeworx/linear", "title": "Linear by Pipeworx",
                "remotes": [{"type": "streamable-http",
                             "url": "https://gateway.pipeworx.io/linear/mcp"}]}},
    {"server": {"name": "io.github.a/sentry-tools", "title": "Sentry tools",
                "remotes": [{"type": "streamable-http",
                             "url": "https://mcp.sentry.dev/mcp/"}]}}
  ]})"));
  PluginRef sentry{.source = CatalogSource::kClaudePlugin,
                   .name = "sentry",
                   .title = "Sentry",
                   .description = "Error monitoring.",
                   .folder = "https://raw.test/s/"};
  PluginRef linear{.source = CatalogSource::kClaudePlugin,
                   .name = "linear",
                   .folder = "https://raw.test/l/"};
  std::vector<std::pair<std::string, CatalogEntry>> plugins;
  plugins.emplace_back(
      sentry.folder,
      *PluginEntry(sentry, R"({"s": {"url": "https://mcp.sentry.dev/mcp"}})", ""));
  plugins.emplace_back(
      linear.folder,
      *PluginEntry(linear, R"({"l": {"url": "https://mcp.linear.app/mcp"}})", ""));
  catalog.SetPlugins(std::move(plugins));

  EXPECT_EQ(catalog.Search("", 20).size(), RecommendedConnectors().size());
  EXPECT_THAT(Ids(catalog.Search("linear", 20)),
              ElementsAre("recommended:linear", "registry:io.pipeworx/linear"));
  // The registry's Linear holds the words; Recommended's Linear stands for it.
  EXPECT_THAT(Ids(catalog.Search("issue tracking", 20)),
              ElementsAre("recommended:linear"));
  EXPECT_THAT(Ids(catalog.Search("sentry", 20)), ElementsAre("claude:sentry"));
  EXPECT_THAT(Ids(catalog.Search("SENTRY error", 20)),
              ElementsAre("claude:sentry"));
  EXPECT_TRUE(catalog.Search("ibkr", 20).empty());
  EXPECT_EQ(catalog.Search("linear", 1).size(), 1u);
  EXPECT_TRUE(catalog.Find("registry:io.pipeworx/linear"));
  EXPECT_TRUE(catalog.Find("recommended:tldraw"));
  EXPECT_FALSE(catalog.Find("registry:nothing"));
}

TEST(ConnectorCatalogTest, KeepsItsCopyAndWhereARefreshStopped) {
  ConnectorCatalog catalog;
  catalog.AddRegistryPage(Page(R"({"servers": [
    {"server": {"name": "a.b/c", "remotes": [{"type": "streamable-http",
                "url": "https://c.dev/mcp", "headers": [{"name": "K",
                "isRequired": true, "isSecret": true}]}]}}]})"));
  PluginRef plugin{.source = CatalogSource::kOpenAiPlugin,
                   .name = "p",
                   .folder = "https://raw.test/o/0123456789abcdef0123456789abcdef01234567/"};
  std::vector<std::pair<std::string, CatalogEntry>> plugins;
  plugins.emplace_back(plugin.folder,
                       *PluginEntry(plugin, R"({"p": {"url": "https://p.dev/mcp"}})", ""));
  catalog.SetPlugins(std::move(plugins));
  catalog.refreshed = base::Time::FromSecondsSinceUnixEpoch(1000);
  catalog.registry_cursor = "a.b/c:1.0.0";
  catalog.registry_pass = base::Time::FromSecondsSinceUnixEpoch(900);

  std::optional<ConnectorCatalog> back = ConnectorCatalog::FromJson(catalog.ToJson());
  ASSERT_TRUE(back);
  EXPECT_EQ(back->refreshed, catalog.refreshed);
  EXPECT_EQ(back->registry_cursor, "a.b/c:1.0.0");
  EXPECT_EQ(back->registry_pass, catalog.registry_pass);
  EXPECT_EQ(back->registry_size(), 1u);
  std::optional<CatalogEntry> entry = back->Find("registry:a.b/c");
  ASSERT_TRUE(entry);
  ASSERT_EQ(entry->fields.size(), 1u);
  EXPECT_TRUE(entry->fields[0].secret);
  ASSERT_TRUE(back->PinnedPlugin(plugin.folder));
  EXPECT_EQ(back->PinnedPlugin(plugin.folder)->url, "https://p.dev/mcp");

  RegistryPage gone = Page(R"({"servers": [{"server": {"name": "a.b/c"},
      "_meta": {"io.modelcontextprotocol.registry/official":
                {"status": "deprecated"}}}]})");
  back->AddRegistryPage(std::move(gone));
  EXPECT_EQ(back->registry_size(), 0u);
  EXPECT_FALSE(ConnectorCatalog::FromJson("not json"));
}

TEST(ConnectorCatalogTest, SameServerIgnoresTheQueryAndClosingSlash) {
  EXPECT_TRUE(SameServer("https://mcp.exa.ai/mcp?client=claude-code-plugin",
                         "https://MCP.exa.ai/mcp/"));
  EXPECT_FALSE(SameServer("https://mcp.exa.ai/mcp", "https://mcp.exa.ai/sse"));
}

}  // namespace
}  // namespace uw
