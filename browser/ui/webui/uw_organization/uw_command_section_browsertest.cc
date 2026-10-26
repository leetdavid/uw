// Copyright 2026 The uw Authors. All rights reserved.

#include <string>

#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/strings/strcat.h"
#include "base/test/run_until.h"
#include "base/threading/thread_restrictions.h"
#include "chrome/browser/headless/test/headless_mode_browsertest.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/grit/generated_resources.h"
#include "chrome/test/base/ui_test_utils.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "ui/base/l10n/l10n_util.h"
#include "url/gurl.h"
#include "uw/browser/tab_organization/window_screenshot.h"
#include "uw/browser/uw_command/command_channel.h"
#include "uw/browser/uw_command/command_install.h"

namespace uw {
namespace {

constexpr char kHelpers[] = R"(
  const find = (root, selector) => {
    const found = root.querySelector(selector);
    if (found) {
      return found;
    }
    for (const element of root.querySelectorAll('*')) {
      const inner = element.shadowRoot && find(element.shadowRoot, selector);
      if (inner) {
        return inner;
      }
    }
    return null;
  };
  const until = (probe) => new Promise((resolve) => {
    const check = () => {
      const value = probe();
      if (value) {
        resolve(value);
      } else {
        setTimeout(check, 20);
      }
    };
    check();
  });
  const section = () => find(document, 'settings-uw-command-section');
  const part = (id) => section()?.shadowRoot?.getElementById(id) ?? null;
  const text = (id) => part(id).textContent.replace(/\s+/g, ' ').trim();
)";

// Drives the uw command's section at the top of Settings, System, with the
// link in a folder of the test's own, so no password is asked.
class UwCommandSectionBrowserTest : public headless::HeadlessModeBrowserTest {
 protected:
  void SetUpOnMainThread() override {
    HeadlessModeBrowserTest::SetUpOnMainThread();
    base::ScopedAllowBlockingForTesting blocking;
    ASSERT_TRUE(folder_.CreateUniqueTempDir());
    link_ = folder_.GetPath().Append("uw");
    SetCommandLinkForTesting(link_);
  }

  void TearDownOnMainThread() override {
    CommandChannel::Get().Close();
    SetCommandLinkForTesting(base::FilePath());
    HeadlessModeBrowserTest::TearDownOnMainThread();
  }

  content::EvalJsResult Run(const std::string& body) {
    return content::EvalJs(
        browser()->tab_strip_model()->GetActiveWebContents(),
        base::StrCat({"(async () => {", kHelpers, body, "})()"}));
  }

  bool LinkExists() {
    base::ScopedAllowBlockingForTesting blocking;
    return base::IsLink(link_);
  }

  static std::string String(int id) { return l10n_util::GetStringUTF8(id); }

  base::ScopedTempDir folder_;
  base::FilePath link_;
};

IN_PROC_BROWSER_TEST_F(UwCommandSectionBrowserTest,
                       InstallLinksTheCommandAndOpensTheChannel) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), GURL("chrome://settings/system")));
  ASSERT_EQ(true, Run("await until(() => part('install') &&"
                      "                   !part('install').disabled);"
                      "return true;"));
  EXPECT_EQ(Run("return text('install');"),
            String(IDS_UW_SETTINGS_COMMAND_INSTALL));
  EXPECT_EQ(Run("return text('description');"),
            "Type uw chat to start a chat, uw mcp add to add a connector, or "
            "uw settings to change a setting. Scripts and agents such as "
            "Claude Code can run it too.");
  EXPECT_EQ(Run("return section().shadowRoot.querySelectorAll("
                "'#description code').length;"),
            3);
  SaveWindowScreenshot(browser(), "uw_command_section_not_installed");

  ASSERT_EQ(true, Run("part('install').click();"
                      "await until(() => part('installed'));"
                      "return true;"));
  EXPECT_TRUE(LinkExists());
  EXPECT_EQ(Run("return text('installed');"),
            base::StrCat({String(IDS_UW_SETTINGS_COMMAND_INSTALLED_AT), " ",
                          link_.value()}));
  EXPECT_TRUE(base::test::RunUntil([] { return CommandChannel::Get().open(); }));
  SaveWindowScreenshot(browser(), "uw_command_section_installed");

  ASSERT_EQ(true, Run("part('uninstall').click();"
                      "await until(() => part('install'));"
                      "return true;"));
  EXPECT_FALSE(LinkExists());
  EXPECT_FALSE(CommandChannel::Get().open());
}

}  // namespace
}  // namespace uw
