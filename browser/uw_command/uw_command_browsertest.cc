// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <string>

#include "base/files/file_util.h"
#include "base/files/scoped_file.h"
#include "base/files/scoped_temp_dir.h"
#include "base/functional/callback_helpers.h"
#include "base/json/json_reader.h"
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

}  // namespace
}  // namespace uw
