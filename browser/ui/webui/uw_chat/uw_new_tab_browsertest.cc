// Copyright 2026 The uw Authors. All rights reserved.

#include <string>

#include "base/strings/strcat.h"
#include "base/test/run_until.h"
#include "chrome/browser/actor/actor_keyed_service.h"
#include "chrome/browser/headless/test/headless_mode_browsertest.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/tabs/tab_enums.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/common/webui_url_constants.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "url/gurl.h"
#include "uw/browser/agent/chat_tab.h"
#include "uw/browser/ai_providers/scoped_memory_keychain.h"
#include "uw/browser/tab_organization/window_screenshot.h"
#include "uw/components/ai_providers/providers.h"

namespace uw {
namespace {

// Finds an element of the page, waiting for it to show.
constexpr char kUntil[] = R"(
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
  const $ = (selector) => document.querySelector(selector);
  const type = (text) => {
    const field = $('#newtab-field');
    const setter = Object.getOwnPropertyDescriptor(
        Object.getPrototypeOf(field), 'value').set;
    setter.call(field, text);
    field.dispatchEvent(new Event('input', {bubbles: true}));
  };
  const press = (key) => $('#newtab-field').dispatchEvent(
      new KeyboardEvent('keydown', {key, bubbles: true}));
)";

// uw's new tab page in every new tab, its box, and what the box does. See
// docs/plans/new-tab-page.md.
class NewTabBrowserTest : public headless::HeadlessModeBrowserTest {
 protected:
  void SetUpOnMainThread() override {
    HeadlessModeBrowserTest::SetUpOnMainThread();
    ASSERT_TRUE(embedded_test_server()->Start());
  }

  void TearDownOnMainThread() override {
    actor::ActorKeyedService::Get(browser()->GetProfile())->ResetForTesting();
    HeadlessModeBrowserTest::TearDownOnMainThread();
  }

  void UseAgentModel() {
    PrefService& prefs = *browser()->GetProfile()->GetPrefs();
    Provider provider = PresetProvider(ProviderPreset::kCustom);
    provider.name = "Test server";
    provider.address = embedded_test_server()->GetURL("/v1").spec();
    SetChoice(prefs, ModelSlot::kAgent, AddProvider(prefs, provider),
              "test-agent");
  }

  // Opens a blank tab in `window`, as Cmd+T does with the command bar off.
  content::WebContents* OpenNewTab(Browser* window) {
    chrome::NewTab(window, NewTabTypes::kNewTabCommand);
    content::WebContents* tab =
        window->tab_strip_model()->GetActiveWebContents();
    EXPECT_TRUE(content::WaitForLoadStop(tab));
    return tab;
  }

  static content::EvalJsResult Run(content::WebContents* tab,
                                   const std::string& script) {
    return content::EvalJs(
        tab, base::StrCat({"(async () => {", kUntil, script, "})()"}));
  }

  ScopedMemoryKeychain keychain_;
};

IN_PROC_BROWSER_TEST_F(NewTabBrowserTest, ANewTabShowsUwsPage) {
  content::WebContents* tab = OpenNewTab(browser());
  EXPECT_EQ(tab->GetPrimaryMainFrame()->GetLastCommittedURL().host(),
            chrome::kChromeUINewTabPageHost);
  // The address bar shows nothing, as on Chromium's new tab page.
  EXPECT_EQ(tab->GetController().GetLastCommittedEntry()->GetVirtualURL(),
            GURL(chrome::kChromeUINewTabURL));
  EXPECT_EQ(true, Run(tab, "return !!(await until(() => $('#newtab-box')));"));
  EXPECT_EQ(true, Run(tab, "return !$('#newtab-private');"));
  SaveWindowScreenshot(browser(), "new_tab_page");
}

IN_PROC_BROWSER_TEST_F(NewTabBrowserTest, APrivateWindowShowsOnlyTheBox) {
  UseAgentModel();
  Browser* incognito = CreateIncognitoBrowser();
  content::WebContents* tab = OpenNewTab(incognito);
  EXPECT_EQ(tab->GetPrimaryMainFrame()->GetLastCommittedURL().host(),
            chrome::kChromeUINewTabPageHost);
  EXPECT_EQ(tab->GetController().GetLastCommittedEntry()->GetVirtualURL(),
            GURL(chrome::kChromeUINewTabURL));
  EXPECT_EQ(true, Run(tab, "return !!(await until(() => $('#newtab-private')));"));
  // Ask has no Once chip there: a private chat never runs on a schedule.
  EXPECT_EQ(true, Run(tab,
                      "$('#newtab-ask').click();"
                      "await until(() => $('#newtab-send'));"
                      "return !$('#newtab-once');"));
  SaveWindowScreenshot(incognito, "new_tab_page_private");
}

IN_PROC_BROWSER_TEST_F(NewTabBrowserTest, AnAddressOpensHere) {
  const GURL page = embedded_test_server()->GetURL("/title2.html");
  content::WebContents* tab = OpenNewTab(browser());
  const int tabs = browser()->tab_strip_model()->count();
  ASSERT_EQ(true, Run(tab, base::StrCat({
                               "await until(() => $('#newtab-field'));"
                               "type('",
                               page.spec(),
                               "');"
                               "return !!(await until(() =>"
                               "    $('#newtab-rows li')));",
                           })));
  ASSERT_EQ(true, Run(tab, "press('Enter'); return true;"));
  EXPECT_TRUE(base::test::RunUntil([&] {
    return browser()->tab_strip_model()->GetActiveWebContents()
               ->GetLastCommittedURL() == page;
  }));
  EXPECT_EQ(browser()->tab_strip_model()->count(), tabs);
}

IN_PROC_BROWSER_TEST_F(NewTabBrowserTest, AskTurnsThisTabIntoAChat) {
  UseAgentModel();
  content::WebContents* tab = OpenNewTab(browser());
  const int tabs = browser()->tab_strip_model()->count();
  ASSERT_EQ(true, Run(tab,
                      "(await until(() => $('#newtab-ask'))).click();"
                      "await until(() => $('textarea#newtab-field'));"
                      "type('Find a tram day pass');"
                      "press('Enter');"
                      "return true;"));
  EXPECT_TRUE(base::test::RunUntil([&] {
    return browser()->tab_strip_model()->GetActiveWebContents()
               ->GetLastCommittedURL()
               .host() == "chat";
  }));
  EXPECT_EQ(browser()->tab_strip_model()->count(), tabs);
  content::WebContents* chat =
      browser()->tab_strip_model()->GetActiveWebContents();
  EXPECT_EQ(true, Run(chat,
                      "return !!(await until(() => document.body.innerText"
                      "    .includes('Find a tram day pass')));"));
}

}  // namespace
}  // namespace uw
