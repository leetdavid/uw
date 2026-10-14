// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include <errno.h>
#include <signal.h>

#include <string>
#include <string_view>

#include "base/environment.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/test/run_until.h"
#include "base/threading/thread_restrictions.h"
#include "chrome/browser/headless/test/headless_mode_browsertest.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/test/base/ui_test_utils.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "ui/events/keycodes/dom/dom_code.h"
#include "ui/events/keycodes/dom/dom_key.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "url/gurl.h"
#include "uw/browser/tab_organization/window_screenshot.h"
#include "uw/browser/terminal/terminal_tab.h"

namespace uw {
namespace {

class UwTerminalBrowserTest : public headless::HeadlessModeBrowserTest {
 protected:
  void SetUpOnMainThread() override {
    HeadlessModeBrowserTest::SetUpOnMainThread();
    base::ScopedAllowBlockingForTesting allow_blocking;
    ASSERT_TRUE(folder_.CreateUniqueTempDir());
    folder_path_ = base::MakeAbsoluteFilePath(folder_.GetPath());
    // zsh finds none of the user's startup files in an empty folder, so the
    // tests do not depend on them.
    base::Environment::Create()->SetVar("ZDOTDIR", folder_path_.value());
  }

  content::WebContents* contents() {
    return browser()->tab_strip_model()->GetActiveWebContents();
  }

  // Opens a terminal tab and waits until its shell is at a prompt, which is
  // when the tab takes the name of the folder.
  content::WebContents* OpenTerminal() {
    chrome::ExecuteCommand(browser(), kNewTerminalCommand);
    content::WebContents* terminal = contents();
    EXPECT_TRUE(IsTerminalUrl(terminal->GetVisibleURL()));
    WaitForPrompt(terminal);
    return terminal;
  }

  void WaitForPrompt(content::WebContents* terminal) {
    EXPECT_TRUE(content::WaitForLoadStop(terminal));
    const std::u16string home =
        base::UTF8ToUTF16(base::GetHomeDir().BaseName().value());
    EXPECT_TRUE(base::test::RunUntil([&] {
      return terminal->GetTitle() == home;
    })) << terminal->GetTitle();
  }

  // Types `text` one key at a time, as a US keyboard would.
  void Type(std::u16string_view text) {
    for (char16_t key : text) {
      if (key == '\r') {
        content::SimulateKeyPress(contents(), ui::DomKey::ENTER,
                                  ui::DomCode::ENTER, ui::VKEY_RETURN, false,
                                  false, false, false);
      } else {
        content::SimulateCharTyped(contents(), key);
      }
    }
  }

  void Press(char16_t key,
             ui::DomCode code,
             ui::KeyboardCode key_code,
             bool control,
             bool alt,
             bool command) {
    content::SimulateKeyPress(contents(), ui::DomKey::FromCharacter(key), code,
                              key_code, control, false, alt, command);
  }

  // Waits until the file `name` in the test folder holds what `done` wants.
  std::string WaitForFile(std::string_view name,
                          base::FunctionRef<bool(const std::string&)> done) {
    base::ScopedAllowBlockingForTesting allow_blocking;
    std::string text;
    EXPECT_TRUE(base::test::RunUntil([&] {
      return base::ReadFileToString(folder_path_.Append(name), &text) &&
             done(text);
    })) << name
        << ": " << text;
    return text;
  }

  bool Exists(std::string_view name) {
    base::ScopedAllowBlockingForTesting allow_blocking;
    return base::PathExists(folder_path_.Append(name));
  }

  std::u16string Folder() { return base::UTF8ToUTF16(folder_path_.value()); }

  base::ScopedTempDir folder_;
  base::FilePath folder_path_;
};

IN_PROC_BROWSER_TEST_F(UwTerminalBrowserTest,
                       OpensWithAPromptAtHomeNamedAfterTheFolder) {
  const int before = browser()->tab_strip_model()->count();
  OpenTerminal();
  EXPECT_EQ(browser()->tab_strip_model()->count(), before + 1);
}

IN_PROC_BROWSER_TEST_F(UwTerminalBrowserTest,
                       TypingReachesTheShellAndTheNameFollowsTheFolder) {
  content::WebContents* terminal = OpenTerminal();
  Type(u"cd " + Folder() + u"\r");
  const std::u16string name =
      base::UTF8ToUTF16(folder_path_.BaseName().value());
  EXPECT_TRUE(
      base::test::RunUntil([&] { return terminal->GetTitle() == name; }));

  Type(u"echo typed > typed\r");
  EXPECT_EQ(WaitForFile("typed",
                        [](const std::string& text) { return !text.empty(); }),
            "typed\n");

  Type(
      u"printf '\\033[1;31mred \\033[32mgreen \\033[34mblue \\033[0m"
      u"\\342\\202\\254 \\346\\274\\242\\345\\255\\227\\n'; touch drawn\r");
  WaitForFile("drawn", [](const std::string&) { return true; });
  SaveWindowScreenshot(browser(), "terminal_tab");
}

// Cmd shortcuts, Ctrl+Tab, and Ctrl+` are the browser's, and Option is Alt.
IN_PROC_BROWSER_TEST_F(UwTerminalBrowserTest, KeysTheBrowserKeepsStayOutOfIt) {
  OpenTerminal();
  Type(u"cd " + Folder() + u" && cat -v > keys\r");
  // The file appears once cat has started.
  WaitForFile("keys", [](const std::string&) { return true; });
  Press('t', ui::DomCode::US_T, ui::VKEY_T, false, false, /*command=*/true);
  Press('`', ui::DomCode::BACKQUOTE, ui::VKEY_OEM_3, /*control=*/true, false,
        false);
  content::SimulateKeyPress(contents(), ui::DomKey::TAB, ui::DomCode::TAB,
                            ui::VKEY_TAB, /*control=*/true, false, false,
                            false);
  Press(u'∫', ui::DomCode::US_B, ui::VKEY_B, false, /*alt=*/true, false);
  Type(u"x\r");
  Press('d', ui::DomCode::US_D, ui::VKEY_D, /*control=*/true, false, false);
  EXPECT_EQ(
      WaitForFile("keys",
                  [](const std::string& text) { return text.ends_with('\n'); }),
      "^[bx\n");
}

IN_PROC_BROWSER_TEST_F(UwTerminalBrowserTest, ClosingTheTabEndsTheShell) {
  content::WebContents* terminal = OpenTerminal();
  Type(u"echo $$ > " + Folder() + u"/pid\r");
  const std::string text = WaitForFile(
      "pid", [](const std::string& text) { return text.ends_with('\n'); });
  int shell = 0;
  ASSERT_TRUE(base::StringToInt(base::TrimWhitespaceASCII(text, base::TRIM_ALL),
                                &shell));
  ASSERT_EQ(kill(shell, 0), 0);

  TabStripModel* tabs = browser()->tab_strip_model();
  tabs->CloseWebContentsAt(tabs->GetIndexOfWebContents(terminal),
                           TabCloseTypes::CLOSE_USER_GESTURE);

  EXPECT_TRUE(base::test::RunUntil(
      [&] { return kill(shell, 0) != 0 && errno == ESRCH; }));
}

IN_PROC_BROWSER_TEST_F(UwTerminalBrowserTest, ExitClosesTheTab) {
  const int before = browser()->tab_strip_model()->count();
  OpenTerminal();
  Type(u"exit\r");
  EXPECT_TRUE(base::test::RunUntil(
      [&] { return browser()->tab_strip_model()->count() == before; }));
}

IN_PROC_BROWSER_TEST_F(UwTerminalBrowserTest, TheAddressCarriesNoCommand) {
  const std::string touch = "touch%20" + folder_path_.value() + "/from-address";
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), GURL("chrome://terminal/" + touch + "%0A?" + touch + "%0A#" +
                      touch + "%0A")));
  WaitForPrompt(contents());
  Type(u"touch " + Folder() + u"/typed\r");
  WaitForFile("typed", [](const std::string&) { return true; });
  EXPECT_FALSE(Exists("from-address"));
}

}  // namespace
}  // namespace uw
