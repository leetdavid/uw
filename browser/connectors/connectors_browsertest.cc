// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/browser/connectors/connectors.h"

#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "base/base_paths.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/functional/bind.h"
#include "base/json/json_writer.h"
#include "base/path_service.h"
#include "base/strings/strcat.h"
#include "base/synchronization/lock.h"
#include "base/test/run_until.h"
#include "base/test/test_future.h"
#include "base/thread_annotations.h"
#include "base/threading/thread_restrictions.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/headless/test/headless_mode_browsertest.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/navigator/browser_navigator.h"
#include "chrome/browser/ui/navigator/browser_navigator_params.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "content/public/test/browser_test.h"
#include "net/base/url_util.h"
#include "net/http/http_status_code.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "net/test/embedded_test_server/http_request.h"
#include "net/test/embedded_test_server/http_response.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "url/gurl.h"
#include "uw/browser/ai_providers/api_key_store.h"
#include "uw/browser/ai_providers/scoped_memory_keychain.h"
#include "uw/browser/connectors/connector_catalog_store.h"
#include "uw/browser/connectors/fake_mcp_server.h"
#include "uw/components/connectors/connector_config.h"
#include "uw/components/connectors/oauth.h"

namespace uw {
namespace {

using ::testing::ElementsAre;
using ::testing::HasSubstr;

std::vector<std::string> Ids(const std::vector<CatalogEntry>& entries) {
  std::vector<std::string> ids;
  for (const CatalogEntry& entry : entries) {
    ids.push_back(entry.id);
  }
  return ids;
}

std::vector<std::string> ToolNames(const Connectors::Info& info) {
  std::vector<std::string> names;
  for (const mcp::Tool& tool : info.tools) {
    names.push_back(tool.name);
  }
  return names;
}

// A web connector the test server serves at /mcp: modern, or legacy with a
// session, which answers tool calls as an event stream.
class WebConnector {
 public:
  WebConnector(net::test_server::EmbeddedTestServer& server, bool legacy)
      : fake_(legacy), legacy_(legacy) {
    server.RegisterRequestHandler(
        base::BindRepeating(&WebConnector::Handle, base::Unretained(this)));
  }

  // The headers of each request, as "method version session".
  std::vector<std::string> seen() {
    base::AutoLock lock(lock_);
    return seen_;
  }

  // Makes /mcp need a token, and serves the sign-in server that gives one.
  // Without registration, it takes no client registration.
  void RequireSignIn(bool registration = true) {
    base::AutoLock lock(lock_);
    sign_in_ = true;
    registration_ = registration;
  }

  // The token given out stops working.
  void Expire() {
    base::AutoLock lock(lock_);
    access_ = "expired";
  }

 private:
  // The sign-in server's pages.
  std::unique_ptr<net::test_server::HttpResponse> SignIn(
      const net::test_server::HttpRequest& request) {
    base::AutoLock lock(lock_);
    auto response = std::make_unique<net::test_server::BasicHttpResponse>();
    response->set_content_type("application/json");
    const std::string base = request.base_url.spec();
    const std::string issuer = base.substr(0, base.size() - 1);
    const GURL url = request.GetURL();
    if (url.path() == "/.well-known/oauth-protected-resource/mcp") {
      response->set_content(base::StrCat(
          {R"({"resource": ")", base, R"(mcp", "authorization_servers": [")",
           issuer, R"("]})"}));
    } else if (url.path() == "/.well-known/oauth-authorization-server") {
      response->set_content(base::StrCat(
          {R"({"issuer": ")", issuer, R"(", "authorization_endpoint": ")", base,
           R"(authorize", "token_endpoint": ")", base, R"(token",)",
           registration_ ? base::StrCat({R"("registration_endpoint": ")", base,
                                         R"(register",)"})
                         : std::string(),
           R"("code_challenge_methods_supported": ["S256"]})"}));
    } else if (url.path() == "/register") {
      response->set_content(R"({"client_id": "client-1"})");
    } else if (url.path() == "/authorize") {
      std::string state;
      net::GetValueForKeyInQuery(url, "state", &state);
      net::GetValueForKeyInQuery(url, "code_challenge", &challenge_);
      response->set_code(net::HTTP_FOUND);
      response->AddCustomHeader(
          "Location", net::AppendQueryParameter(
                          net::AppendQueryParameter(
                              net::AppendQueryParameter(
                                  GURL(oauth::kRedirectUri), "code", "code-1"),
                              "state", state),
                          "iss", issuer)
                          .spec());
    } else if (url.path() == "/token") {
      const GURL form(base::StrCat({"http://form/?", request.content}));
      std::string grant;
      std::string value;
      net::GetValueForKeyInQuery(form, "grant_type", &grant);
      if (grant == "authorization_code" &&
          net::GetValueForKeyInQuery(form, "code_verifier", &value) &&
          oauth::Challenge(value) == challenge_) {
        access_ = "access-1";
        response->set_content(R"({"access_token": "access-1",
            "refresh_token": "refresh-1", "expires_in": 3600})");
      } else if (grant == "refresh_token" &&
                 net::GetValueForKeyInQuery(form, "refresh_token", &value) &&
                 value == "refresh-1") {
        access_ = "access-2";
        response->set_content(R"({"access_token": "access-2"})");
      } else {
        response->set_code(net::HTTP_BAD_REQUEST);
        response->set_content(R"({"error": "invalid_grant"})");
      }
    } else {
      return nullptr;
    }
    return response;
  }

  std::unique_ptr<net::test_server::HttpResponse> Handle(
      const net::test_server::HttpRequest& request) {
    if (request.relative_url != "/mcp") {
      return SignIn(request);
    }
    base::AutoLock lock(lock_);
    auto response = std::make_unique<net::test_server::BasicHttpResponse>();
    auto authorization = request.headers.find("Authorization");
    if (sign_in_ && (authorization == request.headers.end() ||
                     authorization->second != "Bearer " + access_)) {
      response->set_code(net::HTTP_UNAUTHORIZED);
      response->AddCustomHeader(
          "WWW-Authenticate",
          base::StrCat({"Bearer resource_metadata=\"", request.base_url.spec(),
                        ".well-known/oauth-protected-resource/mcp\""}));
      return response;
    }
    const auto header = [&](const std::string& name) {
      auto found = request.headers.find(name);
      return found == request.headers.end() ? std::string("-") : found->second;
    };
    const std::string method =
        legacy_ ? std::string("-") : header("Mcp-Method");
    seen_.push_back(base::StrCat({method, " ", header("MCP-Protocol-Version"),
                                  " ", header("Mcp-Session-Id")}));
    std::optional<std::string> answer = fake_.Answer(request.content);
    if (!answer) {
      response->set_code(net::HTTP_ACCEPTED);
      return response;
    }
    const bool initialize =
        request.content.find("\"initialize\"") != std::string::npos;
    if (legacy_ && fake_.session().empty()) {
      // A legacy server refuses what comes before initialize.
      response->set_code(net::HTTP_BAD_REQUEST);
    }
    if (initialize) {
      response->AddCustomHeader("Mcp-Session-Id", fake_.session());
    }
    if (legacy_ &&
        request.content.find("\"tools/call\"") != std::string::npos) {
      response->set_content_type("text/event-stream");
      response->set_content(base::StrCat(
          {"event: message\ndata: {\"jsonrpc\":\"2.0\",\"method\":",
           "\"notifications/progress\"}\n\nevent: message\ndata: ", *answer,
           "\n\n"}));
      return response;
    }
    response->set_content_type("application/json");
    response->set_content(*answer);
    return response;
  }

  base::Lock lock_;
  FakeMcpServer fake_ GUARDED_BY(lock_);
  const bool legacy_;
  bool sign_in_ GUARDED_BY(lock_) = false;
  bool registration_ GUARDED_BY(lock_) = true;
  std::string access_ GUARDED_BY(lock_) = "none yet";
  std::string challenge_ GUARDED_BY(lock_);
  std::vector<std::string> seen_ GUARDED_BY(lock_);
};

// The MCP Registry and both plugin directories, as a test server serves them
// under /v0 and /raw. The registry's second page fails the first time.
class FakeCatalog {
 public:
  explicit FakeCatalog(net::test_server::EmbeddedTestServer& server) {
    server.RegisterRequestHandler(
        base::BindRepeating(&FakeCatalog::Handle, base::Unretained(this)));
  }

  // The query of each request for a page of the registry.
  std::vector<std::string> pages() {
    base::AutoLock lock(lock_);
    return pages_;
  }

 private:
  static std::string Page(std::string_view name,
                          std::string_view url,
                          std::string_view next) {
    return base::StrCat(
        {R"({"servers": [{"server": {"name": ")", name,
         R"(", "remotes": [{"type": "streamable-http", "url": ")", url,
         R"("}]}}], "metadata": {"nextCursor": ")", next, R"("}})"});
  }

  std::unique_ptr<net::test_server::HttpResponse> Handle(
      const net::test_server::HttpRequest& request) {
    static constexpr std::pair<std::string_view, std::string_view> kFiles[] = {
        {"/raw/anthropics/claude-plugins-official/main/.claude-plugin/"
         "marketplace.json",
         R"({"plugins": [{"name": "sentry", "description": "Errors.",
             "source": "./external_plugins/sentry"}]})"},
        {"/raw/anthropics/claude-plugins-official/main/external_plugins/"
         "sentry/.mcp.json",
         R"({"sentry": {"type": "http", "url": "https://mcp.sentry.dev/mcp"}})"},
        {"/raw/openai/plugins/main/.agents/plugins/marketplace.json",
         R"({"plugins": [{"name": "granola",
             "source": {"source": "local", "path": "./plugins/granola"}}]})"},
        {"/raw/openai/plugins/main/plugins/granola/.mcp.json",
         R"({"mcpServers": {"granola": {"type": "http",
                            "url": "https://mcp.granola.ai/mcp"}}})"},
        {"/raw/openai/plugins/main/plugins/granola/.codex-plugin/plugin.json",
         R"({"description": "Your meetings.",
             "interface": {"displayName": "Granola"}})"},
    };
    const GURL url = request.GetURL();
    auto response = std::make_unique<net::test_server::BasicHttpResponse>();
    response->set_content_type("application/json");
    if (url.path() == "/v0/servers") {
      base::AutoLock lock(lock_);
      pages_.emplace_back(url.query());
      std::string cursor;
      net::GetValueForKeyInQuery(url, "cursor", &cursor);
      if (cursor.empty()) {
        response->set_content(
            Page("io.github.a/first", "https://first.dev/mcp", "second"));
      } else if (!failed_) {
        failed_ = true;
        response->set_code(net::HTTP_SERVICE_UNAVAILABLE);
      } else {
        response->set_content(
            Page("io.github.a/second", "https://second.dev/mcp", ""));
      }
      return response;
    }
    for (const auto& [path, body] : kFiles) {
      if (url.path() == path) {
        response->set_content(std::string(body));
        return response;
      }
    }
    return nullptr;
  }

  base::Lock lock_;
  std::vector<std::string> pages_ GUARDED_BY(lock_);
  bool failed_ GUARDED_BY(lock_) = false;
};

class ConnectorsBrowserTest : public headless::HeadlessModeBrowserTest {
 protected:
  void SetUpOnMainThread() override {
    HeadlessModeBrowserTest::SetUpOnMainThread();
    keychain_ = std::make_unique<ScopedMemoryKeychain>();
    base::ScopedAllowBlockingForTesting allow;
    ASSERT_TRUE(folder_.CreateUniqueTempDir());
    StartUw();
  }

  void TearDownOnMainThread() override {
    Connectors::SetUpForTesting(base::FilePath(), base::FilePath());
    HeadlessModeBrowserTest::TearDownOnMainThread();
  }

  // Starts the connectors as uw does when it starts, with what was saved.
  void StartUw(base::FilePath shell = base::FilePath("/bin/sh")) {
    Connectors::SetUpForTesting(folder_.GetPath(), shell);
    Connectors::Get().Start();
    ASSERT_TRUE(
        base::test::RunUntil([] { return Connectors::Get().loaded(); }));
  }

  ConnectorConfig Local(std::string name, std::vector<std::string> args = {}) {
    base::FilePath program;
    base::PathService::Get(base::DIR_EXE, &program);
    ConnectorConfig connector;
    connector.name = std::move(name);
    connector.command = program.AppendASCII("uw_fake_mcp_server").value();
    connector.args = std::move(args);
    return connector;
  }

  ConnectorConfig Web(std::string name, const GURL& url) {
    ConnectorConfig connector;
    connector.name = std::move(name);
    connector.url = url.spec();
    return connector;
  }

  void Add(ConnectorConfig connector) {
    std::vector<ConnectorConfig> added;
    added.push_back(std::move(connector));
    ASSERT_TRUE(Connectors::Get().Add(std::move(added)).has_value());
  }

  // Waits until `name` is no longer starting.
  const Connectors::Info& Settled(std::string_view name) {
    EXPECT_TRUE(base::test::RunUntil([&] {
      const Connectors::Info* info = Connectors::Get().Find(name);
      return info && info->state != Connectors::State::kStarting;
    }));
    return *Connectors::Get().Find(name);
  }

  base::expected<mcp::Content, std::string> Call(std::string_view name,
                                                 std::string_view tool,
                                                 base::DictValue arguments) {
    base::test::TestFuture<base::expected<mcp::Content, std::string>> done;
    Connectors::Get().CallTool(name, tool, std::move(arguments),
                               done.GetCallback());
    return done.Take();
  }

  // A shell that plays the user's: `interactive` runs when it is started as
  // an interactive shell, before the shell itself.
  base::FilePath FakeShell(std::string_view interactive) {
    base::ScopedAllowBlockingForTesting allow;
    const base::FilePath shell = folder_.GetPath().AppendASCII("shell");
    EXPECT_TRUE(base::WriteFile(
        shell, base::StrCat({"#!/bin/sh\ncase \" $* \" in *\" -i \"*) ",
                             interactive, ";; esac\nexec /bin/sh \"$@\"\n"})));
    EXPECT_TRUE(base::SetPosixFilePermissions(shell, 0700));
    return shell;
  }

  std::unique_ptr<ScopedMemoryKeychain> keychain_;
  base::ScopedTempDir folder_;
};

IN_PROC_BROWSER_TEST_F(ConnectorsBrowserTest,
                       ALocalConnectorListsItsToolsAndRunsOne) {
  Add(Local("fake"));
  const Connectors::Info& info = Settled("fake");
  ASSERT_EQ(info.state, Connectors::State::kReady) << info.error;
  EXPECT_EQ(info.version, "2026-07-28");
  EXPECT_THAT(ToolNames(info),
              ElementsAre("echo", "save_note", "picture", "env", "crash"));
  EXPECT_TRUE(info.tools[0].read_only);
  ASSERT_EQ(info.prompts.size(), 1u);
  EXPECT_EQ(info.prompts[0].name, "greet");

  auto echo = Call("fake", "echo", base::DictValue().Set("text", "hello"));
  ASSERT_TRUE(echo.has_value()) << echo.error();
  EXPECT_EQ(echo->text, "hello");
  auto picture = Call("fake", "picture", {});
  ASSERT_TRUE(picture.has_value());
  EXPECT_THAT(picture->images,
              ElementsAre(HasSubstr("data:image/png;base64,")));

  base::test::TestFuture<base::expected<mcp::Content, std::string>> prompt;
  Connectors::Get().GetPrompt("fake", "greet",
                              base::DictValue().Set("name", "Ada"),
                              prompt.GetCallback());
  EXPECT_EQ(prompt.Get()->text, "Say hello to Ada");
}

IN_PROC_BROWSER_TEST_F(ConnectorsBrowserTest,
                       ALegacyLocalConnectorStartsWithInitialize) {
  Add(Local("old", {"--legacy"}));
  const Connectors::Info& info = Settled("old");
  ASSERT_EQ(info.state, Connectors::State::kReady) << info.error;
  EXPECT_EQ(info.version, "2025-11-25");
  EXPECT_EQ(Call("old", "save_note", base::DictValue().Set("title", "x"))->text,
            "Saved x");
}

IN_PROC_BROWSER_TEST_F(ConnectorsBrowserTest,
                       SecretsGoToKeychainAndReachTheProgram) {
  ConnectorConfig connector = Local("keyed");
  connector.env["FAKE_KEY"] = "s3cret";
  Add(std::move(connector));
  ASSERT_EQ(Settled("keyed").state, Connectors::State::kReady);
  EXPECT_EQ(
      Call("keyed", "env", base::DictValue().Set("name", "FAKE_KEY"))->text,
      "s3cret");
  // Started through the shell, which sets the variables a login shell does.
  EXPECT_NE(Call("keyed", "env", base::DictValue().Set("name", "PATH"))->text,
            "(unset)");

  EXPECT_THAT(ApiKeyStore().GetKey("connector:keyed").value_or(""),
              HasSubstr("s3cret"));
  base::ScopedAllowBlockingForTesting allow;
  std::string saved;
  ASSERT_TRUE(base::test::RunUntil([&] {
    return base::ReadFileToString(
        folder_.GetPath().AppendASCII("connectors.json"), &saved);
  }));
  EXPECT_THAT(saved, HasSubstr("FAKE_KEY"));
  EXPECT_THAT(saved, ::testing::Not(HasSubstr("s3cret")));
}

IN_PROC_BROWSER_TEST_F(ConnectorsBrowserTest,
                       AConnectorThatCannotStartShowsItsLastErrorLine) {
  Add(Local("railway",
            {"--fail=Unauthorized. Please login with railway login"}));
  const Connectors::Info& info = Settled("railway");
  EXPECT_EQ(info.state, Connectors::State::kFailed);
  EXPECT_EQ(info.error, "Unauthorized. Please login with railway login");
  auto call = Call("railway", "echo", {});
  ASSERT_FALSE(call.has_value());
  EXPECT_EQ(call.error(),
            "railway isn't running: Unauthorized. Please login with railway "
            "login");
}

IN_PROC_BROWSER_TEST_F(ConnectorsBrowserTest,
                       AFailedConnectorNamesTheErrorAndShowsItsLog) {
  Add(Local("node",
            {"--fail=ReferenceError: File is not defined\nNode.js v18.6.0"}));
  EXPECT_EQ(Settled("node").error, "ReferenceError: File is not defined");
  EXPECT_TRUE(base::test::RunUntil(
      [] { return !Connectors::Get().Find("node")->log_lines.empty(); }));
  EXPECT_THAT(Connectors::Get().Find("node")->log_lines,
              ElementsAre("starting", "ReferenceError: File is not defined",
                          "Node.js v18.6.0"));

  Connectors::Get().Restart("node");
  EXPECT_TRUE(Connectors::Get().Find("node")->log_lines.empty());
}

IN_PROC_BROWSER_TEST_F(ConnectorsBrowserTest,
                       ALocalConnectorGetsWhatTheTerminalsStartupFilesSet) {
  StartUw(FakeShell("FROM_STARTUP=yes; export FROM_STARTUP"));
  EXPECT_EQ(Connectors::Get().environment_problem(), "");
  Add(Local("fake"));
  ASSERT_EQ(Settled("fake").state, Connectors::State::kReady);
  EXPECT_EQ(
      Call("fake", "env", base::DictValue().Set("name", "FROM_STARTUP"))->text,
      "yes");
}

IN_PROC_BROWSER_TEST_F(ConnectorsBrowserTest,
                       WithoutTheTerminalsEnvironmentTheLoginShellStartsThem) {
  StartUw(FakeShell("exit 1"));
  EXPECT_THAT(Connectors::Get().environment_problem(),
              HasSubstr("couldn't read your terminal's environment"));
  Add(Local("fake"));
  ASSERT_EQ(Settled("fake").state, Connectors::State::kReady);
  EXPECT_EQ(
      Call("fake", "env", base::DictValue().Set("name", "FROM_STARTUP"))->text,
      "(unset)");
}

IN_PROC_BROWSER_TEST_F(ConnectorsBrowserTest,
                       ASavedMcpRemoteConnectorBecomesAWebConnector) {
  ConnectorConfig saved;
  saved.name = "linear";
  saved.command = "npx";
  saved.args = {"-y", "mcp-remote", "https://mcp.linear.app/sse", "--header",
                "Authorization:${AUTH}"};
  saved.env["AUTH"] = "Bearer lin_1";
  ApiKeyStore().SetKey("connector:linear", SecretsToJson(saved));
  base::DictValue value = ConnectorToValue(saved);
  value.Set("on", false);
  {
    base::ScopedAllowBlockingForTesting allow;
    ASSERT_TRUE(base::WriteFile(
        folder_.GetPath().AppendASCII("connectors.json"),
        *base::WriteJson(base::DictValue().Set(
            "connectors", base::ListValue().Append(std::move(value))))));
  }
  StartUw();

  const Connectors::Info* linear = Connectors::Get().Find("linear");
  ASSERT_TRUE(linear);
  EXPECT_TRUE(linear->config.remote());
  EXPECT_EQ(linear->config.url, "https://mcp.linear.app/mcp");
  EXPECT_THAT(ApiKeyStore().GetKey("connector:linear").value_or(""),
              HasSubstr("Bearer lin_1"));
  base::ScopedAllowBlockingForTesting allow;
  EXPECT_TRUE(base::test::RunUntil([&] {
    std::string list;
    return base::ReadFileToString(
               folder_.GetPath().AppendASCII("connectors.json"), &list) &&
           list.find("mcp-remote") == std::string::npos &&
           list.find("\"header_names\":[\"Authorization\"]") !=
               std::string::npos;
  }));
}

IN_PROC_BROWSER_TEST_F(ConnectorsBrowserTest, AProbeTellsAConnectorFromAPage) {
  net::test_server::EmbeddedTestServer server;
  WebConnector web(server, /*legacy=*/false);
  ASSERT_TRUE(server.Start());
  base::test::TestFuture<bool> connector;
  Connectors::Get().Probe(server.GetURL("/mcp"), connector.GetCallback());
  EXPECT_TRUE(connector.Get());

  web.RequireSignIn();
  base::test::TestFuture<bool> signed_out;
  Connectors::Get().Probe(server.GetURL("/mcp"), signed_out.GetCallback());
  EXPECT_TRUE(signed_out.Get());

  base::test::TestFuture<bool> page;
  Connectors::Get().Probe(server.GetURL("/nothing"), page.GetCallback());
  EXPECT_FALSE(page.Get());
}

IN_PROC_BROWSER_TEST_F(ConnectorsBrowserTest,
                       TheCatalogCarriesOnWhereARefreshStopped) {
  net::test_server::EmbeddedTestServer server;
  FakeCatalog fake(server);
  ASSERT_TRUE(server.Start());
  ConnectorCatalogStore& store = ConnectorCatalogStore::Get();
  const auto open = [&] {
    ConnectorCatalogStore::SetUpForTesting(
        folder_.GetPath(), server.GetURL("/"), server.GetURL("/raw/"),
        g_browser_process->shared_url_loader_factory());
    store.Open();
    EXPECT_TRUE(base::test::RunUntil(
        [&] { return store.loaded() && !store.refreshing(); }));
  };

  // The registry's second page fails; the plugins are read all the same.
  open();
  const ConnectorCatalog& catalog = store.catalog();
  EXPECT_TRUE(catalog.Find("registry:io.github.a/first"));
  EXPECT_FALSE(catalog.Find("registry:io.github.a/second"));
  EXPECT_EQ(catalog.registry_cursor, "second");
  EXPECT_TRUE(catalog.refreshed.is_null());
  EXPECT_EQ(catalog.Find("openai:granola")->title, "Granola");
  EXPECT_THAT(Ids(catalog.Search("sentry", 10)), ElementsAre("claude:sentry"));

  // The next time, from where it stopped, read back from its file.
  open();
  EXPECT_TRUE(store.catalog().Find("registry:io.github.a/first"));
  EXPECT_TRUE(store.catalog().Find("registry:io.github.a/second"));
  EXPECT_EQ(store.catalog().registry_cursor, "");
  EXPECT_FALSE(store.catalog().refreshed.is_null());
  ASSERT_EQ(fake.pages().size(), 3u);
  EXPECT_THAT(fake.pages()[2], HasSubstr("cursor=second"));

  // A whole copy less than a day old is kept as it is.
  open();
  EXPECT_EQ(fake.pages().size(), 3u);
  EXPECT_TRUE(store.catalog().Find("registry:io.github.a/second"));
  ConnectorCatalogStore::SetUpForTesting(base::FilePath(), GURL(), GURL(),
                                         nullptr);
}

IN_PROC_BROWSER_TEST_F(ConnectorsBrowserTest,
                       AConnectorThatStopsStartsAgainOnce) {
  Add(Local("fragile"));
  ASSERT_EQ(Settled("fragile").state, Connectors::State::kReady);

  std::ignore = Call("fragile", "crash", {});
  EXPECT_TRUE(base::test::RunUntil([] {
    const Connectors::Info* info = Connectors::Get().Find("fragile");
    return info->state == Connectors::State::kReady;
  }));
  EXPECT_EQ(
      Call("fragile", "echo", base::DictValue().Set("text", "back"))->text,
      "back");

  std::ignore = Call("fragile", "crash", {});
  EXPECT_TRUE(base::test::RunUntil([] {
    return Connectors::Get().Find("fragile")->state ==
           Connectors::State::kFailed;
  }));
  EXPECT_EQ(Connectors::Get().Find("fragile")->error,
            "Stopped twice: crashed on purpose");

  Connectors::Get().Restart("fragile");
  EXPECT_EQ(Settled("fragile").state, Connectors::State::kReady);
}

IN_PROC_BROWSER_TEST_F(ConnectorsBrowserTest,
                       ToolsFromTheLastStartShowBeforeItAnswers) {
  Add(Local("fake"));
  ASSERT_EQ(Settled("fake").state, Connectors::State::kReady);
  Connectors::Get().SetToolOn("fake", "crash", false);
  Connectors::Get().SetAlwaysAllowed("fake", "save_note", true);
  Connectors::Get().SetOn("fake", false);
  EXPECT_EQ(Connectors::Get().Find("fake")->state, Connectors::State::kOff);

  // The files are written in the background; a second start reads them.
  base::ScopedAllowBlockingForTesting allow;
  ASSERT_TRUE(base::test::RunUntil([&] {
    std::string saved;
    return base::ReadFileToString(
               folder_.GetPath().AppendASCII("connectors.json"), &saved) &&
           saved.find("\"on\":false") != std::string::npos &&
           base::PathExists(folder_.GetPath().AppendASCII("fake.json"));
  }));
  StartUw();
  const Connectors::Info* info = Connectors::Get().Find("fake");
  ASSERT_TRUE(info);
  EXPECT_FALSE(info->on);
  EXPECT_EQ(info->state, Connectors::State::kOff);
  EXPECT_EQ(info->tools.size(), 5u);
  EXPECT_THAT(info->tools_off, ElementsAre("crash"));
  EXPECT_THAT(info->always_allowed, ElementsAre("save_note"));

  Connectors::Get().Remove("fake");
  EXPECT_FALSE(Connectors::Get().Find("fake"));
  EXPECT_FALSE(ApiKeyStore().GetKey("connector:fake"));
}

IN_PROC_BROWSER_TEST_F(ConnectorsBrowserTest, AWebConnectorSpeaksModernMcp) {
  net::test_server::EmbeddedTestServer server;
  WebConnector web(server, /*legacy=*/false);
  ASSERT_TRUE(server.Start());
  Add(Web("web", server.GetURL("/mcp")));
  const Connectors::Info& info = Settled("web");
  ASSERT_EQ(info.state, Connectors::State::kReady) << info.error;
  EXPECT_EQ(info.version, "2026-07-28");
  EXPECT_EQ(info.tools.size(), 5u);
  EXPECT_EQ(Call("web", "echo", base::DictValue().Set("text", "hi"))->text,
            "hi");
  EXPECT_THAT(web.seen(), ::testing::Contains("tools/call 2026-07-28 -"));
  EXPECT_EQ(web.seen().front(), "server/discover 2026-07-28 -");
}

IN_PROC_BROWSER_TEST_F(ConnectorsBrowserTest,
                       AWebConnectorFallsBackToALegacySession) {
  net::test_server::EmbeddedTestServer server;
  WebConnector web(server, /*legacy=*/true);
  ASSERT_TRUE(server.Start());
  Add(Web("old-web", server.GetURL("/mcp")));
  const Connectors::Info& info = Settled("old-web");
  ASSERT_EQ(info.state, Connectors::State::kReady) << info.error;
  EXPECT_EQ(info.version, "2025-11-25");
  // The answer comes as an event stream, after a progress notification.
  EXPECT_EQ(
      Call("old-web", "save_note", base::DictValue().Set("title", "t"))->text,
      "Saved t");
  const std::vector<std::string> seen = web.seen();
  ASSERT_GE(seen.size(), 4u);
  // The probe, then initialize without a version, then the rest in the
  // session with the version initialize agreed.
  EXPECT_EQ(seen[0], "- 2026-07-28 -");
  EXPECT_EQ(seen[1], "- - -");
  EXPECT_EQ(seen.back(), "- 2025-11-25 session-1");
}

IN_PROC_BROWSER_TEST_F(ConnectorsBrowserTest,
                       AWebConnectorThatAsksToSignInWaitsForIt) {
  net::test_server::EmbeddedTestServer server;
  WebConnector web(server, /*legacy=*/false);
  web.RequireSignIn();
  ASSERT_TRUE(server.Start());
  Add(Web("locked", server.GetURL("/mcp")));
  EXPECT_EQ(Settled("locked").state, Connectors::State::kNeedsSignIn);
  EXPECT_EQ(Call("locked", "echo", {}).error(),
            "Sign in to locked on uw://ai/connectors first.");
}

// Opens the sign-in page as the connectors page does: in a new tab.
ConnectorSignIn::OpenTab OpenInTab(Browser* browser) {
  return base::BindOnce(
      [](Browser* browser, const GURL& url) {
        NavigateParams params(browser, url, ui::PAGE_TRANSITION_LINK);
        params.disposition = WindowOpenDisposition::NEW_FOREGROUND_TAB;
        Navigate(&params);
        return params.navigated_or_inserted_contents.get();
      },
      browser);
}

IN_PROC_BROWSER_TEST_F(ConnectorsBrowserTest,
                       AWebConnectorSignsInAndRefreshesItsToken) {
  net::test_server::EmbeddedTestServer server;
  WebConnector web(server, /*legacy=*/false);
  web.RequireSignIn();
  ASSERT_TRUE(server.Start());
  Add(Web("linear", server.GetURL("/mcp")));
  ASSERT_EQ(Settled("linear").state, Connectors::State::kNeedsSignIn);
  const int tabs = browser()->tab_strip_model()->count();

  Connectors::Get().SignIn("linear", OpenInTab(browser()));
  EXPECT_TRUE(base::test::RunUntil([] {
    return Connectors::Get().Find("linear")->state == Connectors::State::kReady;
  })) << Connectors::Get().Find("linear")->error;
  // The sign-in tab closed itself once the sign-in came back.
  EXPECT_TRUE(base::test::RunUntil(
      [&] { return browser()->tab_strip_model()->count() == tabs; }));
  EXPECT_EQ(Call("linear", "echo", base::DictValue().Set("text", "in"))->text,
            "in");
  EXPECT_THAT(ApiKeyStore().GetKey("connector-oauth:linear").value_or(""),
              HasSubstr("access-1"));
  EXPECT_THAT(ApiKeyStore()
                  .GetKey(ClientAccount(server.base_url().spec().substr(
                      0, server.base_url().spec().size() - 1)))
                  .value_or(""),
              HasSubstr("client-1"));

  // A token that stopped working is refreshed, and the call goes again.
  web.Expire();
  EXPECT_EQ(
      Call("linear", "echo", base::DictValue().Set("text", "again"))->text,
      "again");
  EXPECT_THAT(ApiKeyStore().GetKey("connector-oauth:linear").value_or(""),
              HasSubstr("access-2"));

  Connectors::Get().SignOut("linear");
  EXPECT_EQ(Connectors::Get().Find("linear")->state,
            Connectors::State::kNeedsSignIn);
  EXPECT_FALSE(ApiKeyStore().GetKey("connector-oauth:linear"));
}

IN_PROC_BROWSER_TEST_F(ConnectorsBrowserTest,
                       ASignInServerWithoutRegistrationCannotSignIn) {
  net::test_server::EmbeddedTestServer server;
  WebConnector web(server, /*legacy=*/false);
  web.RequireSignIn(/*registration=*/false);
  ASSERT_TRUE(server.Start());
  Add(Web("notion", server.GetURL("/mcp")));
  ASSERT_EQ(Settled("notion").state, Connectors::State::kNeedsSignIn);
  Connectors::Get().SignIn("notion", OpenInTab(browser()));
  EXPECT_TRUE(base::test::RunUntil([] {
    return Connectors::Get().Find("notion")->state ==
           Connectors::State::kCantSignIn;
  }));
  EXPECT_EQ(Connectors::Get().Find("notion")->error,
            "This connector's sign-in needs a registration uw doesn't "
            "support yet.");
}

}  // namespace
}  // namespace uw
