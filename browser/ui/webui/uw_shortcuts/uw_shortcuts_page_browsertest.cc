// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include <string>

#include "base/strings/strcat.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/headless/test/headless_mode_browsertest.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "url/gurl.h"
#include "uw/browser/shortcuts/menu_keys.h"
#include "uw/browser/tab_organization/window_screenshot.h"

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
  const page = () => find(document, 'settings-uw-shortcuts-page');
  const rows = () => [...(page()?.shadowRoot?.querySelectorAll('.uw-row') ??
                          [])];
  const row = (title) => rows().find(
      (row) => row.querySelector('.uw-title > div').textContent.trim() ===
               title);
  const text = (element) => element.textContent.trim().replace(/\s+/g, ' ');
)";

class UwShortcutsPageBrowserTest : public headless::HeadlessModeBrowserTest {
 protected:
  // Runs `body` as the body of an async function inside the settings page.
  content::EvalJsResult Run(const std::string& body) {
    return content::EvalJs(
        browser()->tab_strip_model()->GetActiveWebContents(),
        base::StrCat({"(async () => {", kHelpers, body, "})()"}));
  }

  void OpenPage() {
    ASSERT_TRUE(ui_test_utils::NavigateToURL(
        browser(), GURL("chrome://settings/shortcuts")));
    ASSERT_EQ(true, Run("await until(() => row('Keep Here'));"
                        "return true;"));
  }
};

IN_PROC_BROWSER_TEST_F(UwShortcutsPageBrowserTest,
                       ListsChromiumsKeysAndRecordsUws) {
  OpenPage();
  EXPECT_EQ("Command + T",
            Run("return text(row('New Tab').querySelector('.uw-key'));"));
  EXPECT_EQ(true, Run("return !row('New Tab').querySelector("
                      "'cr-shortcut-input');"));
  EXPECT_EQ("Command+Ctrl+K", Run("return row('Keep Here').querySelector("
                                  "'cr-shortcut-input').shortcut;"));
  EXPECT_EQ(true, Run("return page().shadowRoot.getElementById('resetAll')"
                      ".disabled;"));
  SaveWindowScreenshot(browser(), "settings-shortcuts");
}

IN_PROC_BROWSER_TEST_F(UwShortcutsPageBrowserTest,
                       ARecordedKeyTakesChromiumsAndResetAllGivesItBack) {
  OpenPage();
  // What the recorder fires once a key is pressed.
  ASSERT_EQ(true, Run(R"(
    row('Keep Here').querySelector('cr-shortcut-input').dispatchEvent(
        new CustomEvent('shortcut-updated', {detail: 'Command+G'}));
    await until(() => !row('Find Next').querySelector('.uw-note').hidden);
    return true;
  )"));
  EXPECT_EQ("Command + G went to Keep Here",
            Run("return text(row('Find Next').querySelector('.uw-note'));"));
  EXPECT_EQ("No key",
            Run("return text(row('Find Next').querySelector('.uw-key'));"));
  EXPECT_EQ(false, Run("return row('Find Next').querySelector('.uw-reset')"
                       ".hidden;"));
  ASSERT_EQ(true, Run("row('Find Next').scrollIntoView({block: 'center'});"
                      "return true;"));
  SaveWindowScreenshot(browser(), "settings-shortcuts-taken");
  EXPECT_EQ("Command+G", *g_browser_process->local_state()
                              ->GetDict(kMenuKeysPref)
                              .FindString("keep_here"));

  ASSERT_EQ(true, Run(R"(
    page().shadowRoot.getElementById('resetAll').click();
    await until(() => row('Find Next').querySelector('.uw-note').hidden);
    return true;
  )"));
  EXPECT_EQ("Command + G",
            Run("return text(row('Find Next').querySelector('.uw-key'));"));
  EXPECT_TRUE(g_browser_process->local_state()->GetDict(kMenuKeysPref).empty());
}

}  // namespace
}  // namespace uw
