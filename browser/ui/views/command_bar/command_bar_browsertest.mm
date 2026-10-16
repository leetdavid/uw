// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/browser/ui/views/command_bar/command_bar.h"

#import <AppKit/AppKit.h>

#include <atomic>
#include <string>
#include <string_view>

#include "base/run_loop.h"
#include "base/task/sequenced_task_runner.h"
#include "base/test/bind.h"
#include "base/time/time.h"
#include "chrome/browser/headless/test/headless_mode_browsertest.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/search_engines/template_url_service_factory.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/browser/ui/tabs/tab_enums.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/test/base/search_test_utils.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/prefs/pref_service.h"
#include "components/search_engines/template_url.h"
#include "components/search_engines/template_url_data.h"
#include "components/search_engines/template_url_service.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "net/test/embedded_test_server/http_request.h"
#include "ui/events/keycodes/keyboard_codes.h"
#import "ui/events/test/cocoa_test_event_utils.h"
#include "ui/views/widget/widget.h"
#include "uw/browser/tab_organization/ai_connection.h"
#include "uw/browser/tab_organization/window_screenshot.h"

namespace uw {
namespace {

class CommandBarBrowserTest : public headless::HeadlessModeBrowserTest {
 protected:
  CommandBar* bar() { return CommandBar::From(browser()); }

  NSWindow* window() {
    return browser()->GetWindow()->GetNativeWindow().GetNativeNSWindow();
  }

  // Hands a key to the window the way macOS hands it a shortcut.
  void Press(ui::KeyboardCode key, NSEventModifierFlags modifiers) {
    [window()
        performKeyEquivalent:cocoa_test_event_utils::SynthesizeKeyEvent(
                                 window(), /*keyDown=*/true, key, modifiers)];
  }

  // Queues a key behind the ones already waiting, as fast typing does.
  void Queue(ui::KeyboardCode key, NSEventModifierFlags modifiers) {
    for (bool down : {true, false}) {
      [NSApp postEvent:cocoa_test_event_utils::SynthesizeKeyEvent(
                           window(), down, key, modifiers)
               atStart:NO];
    }
  }

  std::string Field() {
    return content::EvalJs(bar()->page(),
                           "document.querySelector('#field').value")
        .ExtractString();
  }

  // Sets the field and tells the page, as typing does.
  void Type(std::string_view text) {
    ASSERT_TRUE(content::ExecJs(bar()->page(), content::JsReplace(R"(
        const field = document.querySelector('#field');
        field.value = $1;
        field.dispatchEvent(new Event('input'));
    )",
                                                                  text)));
  }

  // A key as the field's own key handling sees it.
  void PressInField(std::string_view key) {
    ASSERT_TRUE(content::ExecJs(
        bar()->page(),
        content::JsReplace("document.querySelector('#field').dispatchEvent("
                           "new KeyboardEvent('keydown', {key: $1}))",
                           key)));
  }

  // Each row's kind label, in order.
  std::string Kinds() {
    return content::EvalJs(bar()->page(),
                           "[...document.querySelectorAll('#results .kind')]"
                           ".map(kind => kind.textContent).join(',')")
        .ExtractString();
  }

  // Lets uw finish placing tabs after a change to the tab strip.
  void Settle() { Wait(base::Milliseconds(300)); }

  int Rows() {
    return content::EvalJs(bar()->page(),
                           "document.querySelectorAll('#results li').length")
        .ExtractInt();
  }

  void Wait(base::TimeDelta delay) {
    base::RunLoop loop;
    base::SequencedTaskRunner::GetCurrentDefault()->PostDelayedTask(
        FROM_HERE, loop.QuitClosure(), delay);
    loop.Run();
  }

  // Asks the page every 20 ms until `within` has passed.
  template <typename Condition>
  bool Eventually(Condition condition,
                  base::TimeDelta within = base::Seconds(2)) {
    const base::TimeTicks deadline = base::TimeTicks::Now() + within;
    while (base::TimeTicks::Now() < deadline) {
      if (condition()) {
        return true;
      }
      Wait(base::Milliseconds(20));
    }
    return condition();
  }
};

IN_PROC_BROWSER_TEST_F(CommandBarBrowserTest, CmdTOpensItWithoutATab) {
  ASSERT_TRUE(bar()->page());
  const int tabs = browser()->tab_strip_model()->count();

  Press(ui::VKEY_T, NSEventModifierFlagCommand);

  EXPECT_TRUE(bar()->IsShowing());
  EXPECT_EQ(tabs, browser()->tab_strip_model()->count());
}

IN_PROC_BROWSER_TEST_F(CommandBarBrowserTest, TurnedOffCmdTOpensATab) {
  browser()->GetProfile()->GetPrefs()->SetBoolean(kCommandBarEnabledPref,
                                                  false);
  EXPECT_FALSE(bar()->page());
  const int tabs = browser()->tab_strip_model()->count();

  Press(ui::VKEY_T, NSEventModifierFlagCommand);

  EXPECT_FALSE(bar()->IsShowing());
  EXPECT_EQ(tabs + 1, browser()->tab_strip_model()->count());
}

IN_PROC_BROWSER_TEST_F(CommandBarBrowserTest, TurningItOffClosesIt) {
  Press(ui::VKEY_T, NSEventModifierFlagCommand);
  ASSERT_TRUE(bar()->IsShowing());

  browser()->GetProfile()->GetPrefs()->SetBoolean(kCommandBarEnabledPref,
                                                  false);

  EXPECT_FALSE(bar()->IsShowing());
  EXPECT_FALSE(bar()->page());
}

// The keyboard tests need a real window: a headless panel never takes focus,
// so its page drops the keys. Run them with --run-manual --headful-mode.
IN_PROC_BROWSER_TEST_F(CommandBarBrowserTest,
                       MANUAL_EscapeClosesItAndOpensNothing) {
  const int tabs = browser()->tab_strip_model()->count();
  Press(ui::VKEY_T, NSEventModifierFlagCommand);
  ASSERT_TRUE(bar()->IsShowing());

  Queue(ui::VKEY_ESCAPE, 0);

  EXPECT_TRUE(Eventually([&] { return !bar()->IsShowing(); }));
  EXPECT_EQ(tabs, browser()->tab_strip_model()->count());
}

// Keys typed right after Cmd+T land in its field in order: the first time,
// before the panel's page has drawn and while the browser refuses its keys, and
// again once it has. They must land well inside the one second the browser
// holds keys for.
IN_PROC_BROWSER_TEST_F(CommandBarBrowserTest,
                       MANUAL_KeysTypedRightAfterCmdTLand) {
  ASSERT_TRUE(content::WaitForLoadStop(bar()->page()));

  for (std::string_view word : {"github", "uw"}) {
    Queue(ui::VKEY_T, NSEventModifierFlagCommand);
    for (char letter : word) {
      Queue(static_cast<ui::KeyboardCode>(ui::VKEY_A + letter - 'a'), 0);
    }

    EXPECT_TRUE(
        Eventually([&] { return Field() == word; }, base::Milliseconds(800)))
        << "The field holds: " << Field();

    Queue(ui::VKEY_ESCAPE, 0);
    ASSERT_TRUE(Eventually([&] { return !bar()->IsShowing(); }));
  }
}

IN_PROC_BROWSER_TEST_F(CommandBarBrowserTest,
                       APickedAddressOpensWhereCmdTPutsABlankTab) {
  ASSERT_TRUE(embedded_test_server()->Start());
  ASSERT_TRUE(content::WaitForLoadStop(bar()->page()));
  TabStripModel* tabs = browser()->tab_strip_model();
  ASSERT_TRUE(ui_test_utils::NavigateToURLWithDisposition(
      browser(), embedded_test_server()->GetURL("/title1.html"),
      WindowOpenDisposition::NEW_FOREGROUND_TAB,
      ui_test_utils::BROWSER_TEST_WAIT_FOR_LOAD_STOP));
  tabs->ActivateTabAt(0);
  Settle();
  // Where a blank tab goes when Cmd+T opens one.
  chrome::NewTab(browser(), NewTabTypes::kNewTabCommand);
  Settle();
  const int blank = tabs->active_index();
  tabs->CloseWebContentsAt(blank, TabCloseTypes::CLOSE_NONE);
  tabs->ActivateTabAt(0);
  Settle();
  const int count = tabs->count();

  Press(ui::VKEY_T, NSEventModifierFlagCommand);
  const GURL page = embedded_test_server()->GetURL("/title2.html");
  Type(page.spec());
  ASSERT_TRUE(Eventually([&] { return Rows() > 0; }));
  PressInField("Enter");

  EXPECT_TRUE(Eventually([&] {
    return tabs->count() == count + 1 &&
           tabs->GetActiveWebContents()->GetVisibleURL() == page;
  }));
  Settle();
  EXPECT_EQ(blank, tabs->active_index());
  EXPECT_FALSE(bar()->IsShowing());
}

IN_PROC_BROWSER_TEST_F(CommandBarBrowserTest, AnOpenTabRowSwitchesToIt) {
  ASSERT_TRUE(embedded_test_server()->Start());
  ASSERT_TRUE(content::WaitForLoadStop(bar()->page()));
  TabStripModel* tabs = browser()->tab_strip_model();
  // Chromium closes a blank tab with no history when you switch away from it,
  // so the tab you start from shows a page, as it does in use.
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("/title1.html")));
  ASSERT_TRUE(ui_test_utils::NavigateToURLWithDisposition(
      browser(), embedded_test_server()->GetURL("/title2.html"),
      WindowOpenDisposition::NEW_FOREGROUND_TAB,
      ui_test_utils::BROWSER_TEST_WAIT_FOR_LOAD_STOP));
  content::WebContents* open = tabs->GetActiveWebContents();
  tabs->ActivateTabAt(0);
  Settle();
  const int count = tabs->count();

  Press(ui::VKEY_T, NSEventModifierFlagCommand);
  // The page's title, "Title Of Awesomeness", finds it once its visit is
  // indexed.
  ASSERT_TRUE(Eventually(
      [&] {
        Type("awesomeness");
        Wait(base::Milliseconds(100));
        return Kinds().find("Tab") != std::string::npos;
      },
      base::Seconds(5)))
      << "The rows are: " << Kinds();
  const int row =
      content::EvalJs(bar()->page(),
                      "[...document.querySelectorAll('#results .kind')]"
                      ".findIndex(kind => kind.textContent === 'Tab')")
          .ExtractInt();
  for (int i = 0; i < row; ++i) {
    PressInField("ArrowDown");
  }
  EXPECT_EQ("Switch to tab",
            content::EvalJs(bar()->page(),
                            "document.querySelector('#action').textContent"));
  PressInField("Enter");

  EXPECT_TRUE(Eventually([&] { return tabs->GetActiveWebContents() == open; }));
  EXPECT_EQ(count, tabs->count());
}

// Only results ready at once are asked for, so the search engine hears of
// nothing until a row is picked.
IN_PROC_BROWSER_TEST_F(CommandBarBrowserTest,
                       TypingSendsNothingToTheSearchEngine) {
  std::atomic<int> suggestions = 0;
  std::atomic<int> searches = 0;
  net::EmbeddedTestServer& server = embedded_https_test_server();
  server.RegisterRequestMonitor(base::BindLambdaForTesting(
      [&](const net::test_server::HttpRequest& request) {
        if (request.relative_url.starts_with("/suggest")) {
          ++suggestions;
        } else if (request.relative_url.starts_with("/search")) {
          ++searches;
        }
      }));
  ASSERT_TRUE(server.Start());
  TemplateURLService* engines =
      TemplateURLServiceFactory::GetForProfile(browser()->GetProfile());
  search_test_utils::WaitForTemplateURLServiceToLoad(engines);
  TemplateURLData data;
  data.SetShortName(u"Test search");
  data.SetKeyword(u"test");
  data.SetURL(server.GetURL("/search?q={searchTerms}").spec());
  data.suggestions_url = server.GetURL("/suggest?q={searchTerms}").spec();
  engines->SetUserSelectedDefaultSearchProvider(
      engines->Add(std::make_unique<TemplateURL>(data)));
  ASSERT_TRUE(content::WaitForLoadStop(bar()->page()));

  Press(ui::VKEY_T, NSEventModifierFlagCommand);
  for (std::string_view text : {"l", "li", "lisbon", "lisbon hotels"}) {
    Type(text);
  }
  ASSERT_TRUE(Eventually([&] { return Rows() > 0; }));
  Wait(base::Seconds(1));
  EXPECT_EQ(0, suggestions);

  PressInField("Enter");
  EXPECT_TRUE(Eventually([&] { return searches > 0; }));
}

// The look, for comparing with the design: run in the VM with
// UW_SCREENSHOT_DIR set.
IN_PROC_BROWSER_TEST_F(CommandBarBrowserTest, MANUAL_TheBarAsYouType) {
  ASSERT_TRUE(embedded_test_server()->Start());
  for (std::string_view path :
       {"/title1.html", "/title2.html", "/title3.html"}) {
    ASSERT_TRUE(ui_test_utils::NavigateToURLWithDisposition(
        browser(), embedded_test_server()->GetURL(path),
        WindowOpenDisposition::NEW_FOREGROUND_TAB,
        ui_test_utils::BROWSER_TEST_WAIT_FOR_LOAD_STOP));
  }
  ASSERT_TRUE(content::WaitForLoadStop(bar()->page()));
  Press(ui::VKEY_T, NSEventModifierFlagCommand);
  ASSERT_TRUE(Eventually(
      [&] {
        Type("title");
        Wait(base::Milliseconds(100));
        return Rows() > 2;
      },
      base::Seconds(5)));
  Wait(base::Milliseconds(500));

  SaveWindowScreenshot(browser(), "command-bar-as-you-type");
  SaveWidgetScreenshot(views::Widget::GetTopLevelWidgetForNativeView(
                           bar()->page()->GetNativeView()),
                       "command-bar-panel");
}

IN_PROC_BROWSER_TEST_F(CommandBarBrowserTest, TypingListsSuggestions) {
  ASSERT_TRUE(content::WaitForLoadStop(bar()->page()));
  Press(ui::VKEY_T, NSEventModifierFlagCommand);

  ASSERT_TRUE(content::ExecJs(bar()->page(), R"(
      const field = document.querySelector('#field');
      field.value = 'uw browser';
      field.dispatchEvent(new Event('input'));
  )"));

  EXPECT_TRUE(Eventually([&] { return Rows() > 0; }));
}

}  // namespace
}  // namespace uw
