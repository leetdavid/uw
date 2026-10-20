// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/browser/connectors/connectors.h"

#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <vector>

#include "base/base_paths.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/functional/bind.h"
#include "base/path_service.h"
#include "base/strings/strcat.h"
#include "base/synchronization/lock.h"
#include "base/test/run_until.h"
#include "base/test/test_future.h"
#include "base/thread_annotations.h"
#include "base/threading/thread_restrictions.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/headless/test/headless_mode_browsertest.h"
#include "content/public/test/browser_test.h"
#include "net/http/http_status_code.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "net/test/embedded_test_server/http_request.h"
#include "net/test/embedded_test_server/http_response.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "url/gurl.h"
#include "uw/browser/ai_providers/api_key_store.h"
#include "uw/browser/ai_providers/scoped_memory_keychain.h"
#include "uw/browser/connectors/fake_mcp_server.h"
#include "uw/components/connectors/connector_config.h"

namespace uw {
namespace {

using ::testing::ElementsAre;
using ::testing::HasSubstr;

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

  void RequireSignIn() {
    base::AutoLock lock(lock_);
    sign_in_ = true;
  }

 private:
  std::unique_ptr<net::test_server::HttpResponse> Handle(
      const net::test_server::HttpRequest& request) {
    if (request.relative_url != "/mcp") {
      return nullptr;
    }
    base::AutoLock lock(lock_);
    auto response = std::make_unique<net::test_server::BasicHttpResponse>();
    if (sign_in_ && !request.headers.contains("Authorization")) {
      response->set_code(net::HTTP_UNAUTHORIZED);
      response->AddCustomHeader(
          "WWW-Authenticate",
          "Bearer resource_metadata=\"https://auth.test/.well-known/"
          "oauth-protected-resource\"");
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
  std::vector<std::string> seen_ GUARDED_BY(lock_);
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
  void StartUw() {
    Connectors::SetUpForTesting(folder_.GetPath(), base::FilePath("/bin/sh"));
    Connectors::Get().Start(g_browser_process->shared_url_loader_factory());
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

}  // namespace
}  // namespace uw
