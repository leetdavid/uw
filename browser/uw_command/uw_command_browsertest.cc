// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <memory>
#include <string>

#include "base/base_paths.h"
#include "base/files/file_util.h"
#include "base/files/scoped_file.h"
#include "base/files/scoped_temp_dir.h"
#include "base/functional/callback_helpers.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/path_service.h"
#include "base/run_loop.h"
#include "base/strings/string_util.h"
#include "base/task/thread_pool.h"
#include "base/test/test_future.h"
#include "base/threading/thread_restrictions.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/headless/test/headless_mode_browsertest.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "components/prefs/pref_service.h"
#include "components/version_info/version_info.h"
#include "content/public/browser/browser_task_traits.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/test/browser_test.h"
#include "net/http/http_status_code.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "net/test/embedded_test_server/http_request.h"
#include "net/test/embedded_test_server/http_response.h"
#include "uw/browser/ai_providers/scoped_memory_keychain.h"
#include "uw/browser/connectors/connectors.h"
#include "uw/browser/tab_organization/ai_connection.h"
#include "uw/browser/uw_command/command_channel.h"
#include "uw/browser/workspaces/workspace_list.h"
#include "uw/browser/workspaces/workspace_theme.h"
#include "uw/components/uw_command/command_settings.h"

namespace uw {
namespace {

// Sends one line on a new connection and reads one line back. Blocks, so it
// runs off the UI thread, which answers.
std::string Exchange(const base::FilePath& socket, const std::string& line) {
  base::ScopedFD fd(::socket(AF_UNIX, SOCK_STREAM, 0));
  sockaddr_un address = {.sun_family = AF_UNIX};
  base::strlcpy(address.sun_path, socket.value().c_str(),
                sizeof(address.sun_path));
  timeval timeout = {.tv_sec = 20};
  setsockopt(fd.get(), SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
  if (connect(fd.get(), reinterpret_cast<sockaddr*>(&address),
              sizeof(address)) != 0) {
    return "not listening";
  }
  const std::string sent = line + "\n";
  if (!base::WriteFileDescriptor(fd.get(), sent)) {
    return "not sent";
  }
  std::string reply;
  char byte;
  while (read(fd.get(), &byte, 1) == 1 && byte != '\n') {
    reply += byte;
  }
  return reply;
}

class UwCommandBrowserTest : public headless::HeadlessModeBrowserTest {
 protected:
  void SetUpOnMainThread() override {
    headless::HeadlessModeBrowserTest::SetUpOnMainThread();
    base::ScopedAllowBlockingForTesting blocking;
    // A socket's path must stay short, so it lives under /tmp.
    ASSERT_TRUE(folder_.CreateUniqueTempDirUnderPath(base::FilePath("/tmp")));
    socket_ = folder_.GetPath().Append(kCommandSocketName);
    CommandChannel::Get().OpenForTesting(socket_);
    WaitForTheIoThread();
    work_ = WorkspaceList::Get().Add("Work", "💼", profile()->GetBaseName());
    side_ = WorkspaceList::Get().Add("Side projects", "🧪",
                                     profile()->GetBaseName());
  }

  void TearDownOnMainThread() override {
    CommandChannel::Get().Close();
    headless::HeadlessModeBrowserTest::TearDownOnMainThread();
  }

  Profile* profile() { return browser()->GetProfile(); }

  // The socket opens and closes on the IO thread.
  static void WaitForTheIoThread() {
    base::RunLoop done;
    content::GetIOThreadTaskRunner({})->PostTaskAndReply(
        FROM_HERE, base::DoNothing(), done.QuitClosure());
    done.Run();
  }

  // The reply to `method` with `params`, as JSON.
  base::DictValue Ask(const std::string& method,
                      const std::string& params = "{}") {
    base::test::TestFuture<std::string> reply;
    base::ThreadPool::PostTaskAndReplyWithResult(
        FROM_HERE, {base::MayBlock()},
        base::BindOnce(&Exchange, socket_,
                       R"({"jsonrpc":"2.0","id":7,"method":")" + method +
                           R"(","params":)" + params + "}"),
        reply.GetCallback());
    std::optional<base::DictValue> parsed = base::JSONReader::ReadDict(
        reply.Get(), base::JSON_PARSE_RFC);
    EXPECT_TRUE(parsed) << reply.Get();
    return parsed ? std::move(*parsed) : base::DictValue();
  }

  base::ScopedTempDir folder_;
  base::FilePath socket_;
  std::string work_;
  std::string side_;
};

IN_PROC_BROWSER_TEST_F(UwCommandBrowserTest, HelloAnswersWithUwsVersion) {
  base::DictValue reply = Ask("hello", R"({"version":"dev"})");
  EXPECT_EQ(reply.FindInt("id"), 7);
  EXPECT_EQ(*reply.FindStringByDottedPath("result.version"),
            version_info::GetVersionNumber());
}

IN_PROC_BROWSER_TEST_F(UwCommandBrowserTest,
                       AProfileSettingChangesForEveryWorkspaceOfTheProfile) {
  base::DictValue reply =
      Ask("settings.set",
          R"({"workspace":"side projects","name":"organizer.enabled",)"
          R"("value":"false"})");
  EXPECT_FALSE(profile()->GetPrefs()->GetBoolean(kOrganizationEnabledPref));
  EXPECT_EQ(*reply.FindBoolByDottedPath("result.value"), false);
  const base::ListValue* applies_to =
      reply.FindListByDottedPath("result.appliesTo");
  ASSERT_TRUE(applies_to);
  EXPECT_TRUE(applies_to->contains("Work"));
  EXPECT_TRUE(applies_to->contains("Side projects"));
}

IN_PROC_BROWSER_TEST_F(UwCommandBrowserTest,
                       AThemeSettingChangesOneWorkspace) {
  const int side_grain = ThemeOfWorkspace(side_, profile()).grain;
  base::DictValue reply = Ask(
      "settings.set",
      R"({"workspace":"Work","name":"theme.grain","value":"40"})");
  EXPECT_EQ(reply.FindIntByDottedPath("result.value"), 40);
  EXPECT_EQ(ThemeOfWorkspace(work_, profile()).grain, 40);
  EXPECT_EQ(ThemeOfWorkspace(side_, profile()).grain, side_grain);
  EXPECT_EQ(
      Ask("settings.get", R"({"workspace":"Work","name":"theme.grain"})")
          .FindIntByDottedPath("result.value"),
      40);
}

IN_PROC_BROWSER_TEST_F(UwCommandBrowserTest, ListGivesEverySettingUwKeeps) {
  base::DictValue reply = Ask("settings.list", R"({"workspace":"Work"})");
  const base::ListValue* settings =
      reply.FindListByDottedPath("result.settings");
  ASSERT_TRUE(settings);
  EXPECT_EQ(settings->size(), CommandSettings().size());
  for (const CommandSetting& setting : CommandSettings()) {
    if (setting.scope == CommandSetting::Scope::kProfile) {
      EXPECT_TRUE(profile()->GetPrefs()->FindPreference(setting.pref))
          << setting.name;
    } else if (setting.scope == CommandSetting::Scope::kEverywhere) {
      EXPECT_TRUE(
          g_browser_process->local_state()->FindPreference(setting.pref))
          << setting.name;
    }
  }
}

IN_PROC_BROWSER_TEST_F(UwCommandBrowserTest, MistakesAreWrongUsage) {
  base::DictValue refused = Ask(
      "settings.set", R"({"name":"theme.grain","value":"140"})");
  EXPECT_EQ(refused.FindIntByDottedPath("error.code"), 2);
  EXPECT_EQ(*refused.FindStringByDottedPath("error.message"),
            "theme.grain takes 0 to 100.");

  base::DictValue nameless = Ask("settings.get", R"({"name":"theme.nope"})");
  EXPECT_EQ(nameless.FindIntByDottedPath("error.code"), 2);

  base::DictValue nowhere =
      Ask("settings.list", R"({"workspace":"Elsewhere"})");
  EXPECT_EQ(nowhere.FindIntByDottedPath("error.code"), 2);
  EXPECT_TRUE(base::StartsWith(
      *nowhere.FindStringByDottedPath("error.message"),
      "No workspace is called Elsewhere. Workspaces: "));

  EXPECT_EQ(Ask("chats.list").FindIntByDottedPath("error.code"), -32601);
}

IN_PROC_BROWSER_TEST_F(UwCommandBrowserTest, ACurrencyNeedsRates) {
  base::DictValue unknown = Ask(
      "settings.set", R"({"name":"command-bar.currency","value":"HKD"})");
  EXPECT_EQ(unknown.FindIntByDottedPath("error.code"), 2);

  base::DictValue rates;
  rates.SetByDottedPath("per_euro.HKD", 9.1);
  profile()->GetPrefs()->SetDict(kCommandBarRatesPref, std::move(rates));
  base::DictValue known = Ask(
      "settings.set", R"({"name":"command-bar.currency","value":"hkd"})");
  EXPECT_EQ(*known.FindStringByDottedPath("result.value"), "HKD");
  EXPECT_EQ(profile()->GetPrefs()->GetString(kCommandBarCurrencyPref), "HKD");

  Ask("settings.set", R"({"name":"command-bar.currency","value":"auto"})");
  EXPECT_EQ(profile()->GetPrefs()->GetString(kCommandBarCurrencyPref), "");
}

IN_PROC_BROWSER_TEST_F(UwCommandBrowserTest, AClosedChannelIsNotListening) {
  CommandChannel::Get().Close();
  WaitForTheIoThread();
  base::test::TestFuture<std::string> reply;
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::MayBlock()},
      base::BindOnce(&Exchange, socket_, R"({"id":1,"method":"hello"})"),
      reply.GetCallback());
  EXPECT_EQ(reply.Get(), "not listening");
}

// Drives uw mcp's requests, with the connectors kept in a folder of the
// test's own and their keys in memory.
class UwCommandConnectorsBrowserTest : public UwCommandBrowserTest {
 protected:
  void SetUpOnMainThread() override {
    UwCommandBrowserTest::SetUpOnMainThread();
    keychain_ = std::make_unique<ScopedMemoryKeychain>();
    base::ScopedAllowBlockingForTesting blocking;
    ASSERT_TRUE(connectors_.CreateUniqueTempDir());
    Connectors::SetUpForTesting(connectors_.GetPath(),
                                base::FilePath("/bin/sh"));
  }

  void TearDownOnMainThread() override {
    Connectors::SetUpForTesting(base::FilePath(), base::FilePath());
    UwCommandBrowserTest::TearDownOnMainThread();
  }

  // The paste box's JSON for one connector.
  static std::string Pasted(const std::string& name, base::DictValue server) {
    base::DictValue servers;
    servers.Set(name, std::move(server));
    base::DictValue json;
    json.Set("mcpServers", std::move(servers));
    return base::WriteJson(json).value_or("");
  }

  // The fake MCP server, with a key.
  static base::DictValue FakeServer() {
    base::FilePath program;
    base::PathService::Get(base::DIR_EXE, &program);
    base::DictValue env;
    env.Set("FILES_TOKEN", "sk-test");
    base::DictValue server;
    server.Set("command", program.AppendASCII("uw_fake_mcp_server").value());
    server.Set("env", std::move(env));
    return server;
  }

  base::DictValue AskWith(const std::string& method,
                          const base::DictValue& params) {
    return Ask(method, base::WriteJson(params).value_or("{}"));
  }

  base::ScopedTempDir connectors_;
  std::unique_ptr<ScopedMemoryKeychain> keychain_;
};

IN_PROC_BROWSER_TEST_F(UwCommandConnectorsBrowserTest,
                       AddListAndRemoveALocalConnector) {
  base::DictValue add;
  add.Set("json", Pasted("files", FakeServer()));
  base::DictValue added = AskWith("connectors.add", add);
  const base::ListValue* connectors =
      added.FindListByDottedPath("result.connectors");
  ASSERT_TRUE(connectors && connectors->size() == 1u) << added;
  const base::DictValue& files = (*connectors)[0].GetDict();
  EXPECT_EQ(*files.FindString("name"), "files");
  EXPECT_EQ(*files.FindString("kind"), "local");
  EXPECT_EQ(*files.FindString("state"), "ready");
  EXPECT_GT(files.FindInt("tools").value_or(0), 0);
  EXPECT_EQ(*files.FindList("secrets"), base::ListValue().Append("FILES_TOKEN"));

  EXPECT_EQ(AskWith("connectors.add", add).FindIntByDottedPath("error.code"),
            2);
  base::DictValue listed = Ask("connectors.list");
  EXPECT_EQ(listed.FindListByDottedPath("result.connectors")->size(), 1u);

  base::DictValue named;
  named.Set("name", "files");
  base::DictValue removed = AskWith("connectors.remove", named);
  EXPECT_EQ(*removed.FindStringByDottedPath("result.name"), "files");
  base::DictValue after = Ask("connectors.list");
  EXPECT_TRUE(after.FindListByDottedPath("result.connectors")->empty());
  EXPECT_EQ(
      AskWith("connectors.remove", named).FindIntByDottedPath("error.code"), 2);
}

IN_PROC_BROWSER_TEST_F(UwCommandConnectorsBrowserTest,
                       OnlyAWebConnectorSignsIn) {
  base::DictValue add;
  add.Set("json", Pasted("files", FakeServer()));
  AskWith("connectors.add", add);
  base::DictValue named;
  named.Set("name", "files");
  base::DictValue refused = AskWith("connectors.signIn", named);
  EXPECT_EQ(refused.FindIntByDottedPath("error.code"), 2);
  EXPECT_EQ(*refused.FindStringByDottedPath("error.message"),
            "files runs on this Mac and needs no sign-in.");
}

// A web connector that asks for a sign-in waits for one. A sign-in that
// can't start says so, rather than leaving the command waiting.
IN_PROC_BROWSER_TEST_F(UwCommandConnectorsBrowserTest,
                       AWebConnectorWaitsForItsSignIn) {
  net::EmbeddedTestServer server;
  server.RegisterRequestHandler(base::BindRepeating(
      [](const net::test_server::HttpRequest&)
          -> std::unique_ptr<net::test_server::HttpResponse> {
        auto response = std::make_unique<net::test_server::BasicHttpResponse>();
        response->set_code(net::HTTP_UNAUTHORIZED);
        response->AddCustomHeader("WWW-Authenticate", "Bearer");
        return response;
      }));
  ASSERT_TRUE(server.Start());
  base::DictValue web;
  web.Set("type", "http");
  web.Set("url", server.GetURL("/mcp").spec());
  base::DictValue add;
  add.Set("json", Pasted("web", std::move(web)));
  base::DictValue added = AskWith("connectors.add", add);
  const base::ListValue* connectors =
      added.FindListByDottedPath("result.connectors");
  ASSERT_TRUE(connectors && connectors->size() == 1u) << added;
  EXPECT_EQ(*(*connectors)[0].GetDict().FindString("state"), "needs-sign-in");

  base::DictValue named;
  named.Set("name", "web");
  base::DictValue signed_in = AskWith("connectors.signIn", named);
  EXPECT_EQ(signed_in.FindIntByDottedPath("error.code"), 1) << signed_in;
  EXPECT_TRUE(base::StartsWith(
      *signed_in.FindStringByDottedPath("error.message"),
      "The sign-in to web didn't finish"));
}

}  // namespace
}  // namespace uw
