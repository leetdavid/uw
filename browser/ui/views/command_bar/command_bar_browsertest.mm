// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/browser/ui/views/command_bar/command_bar.h"

#import <AppKit/AppKit.h>

#include <algorithm>
#include <atomic>
#include <optional>
#include <string>
#include <string_view>

#include "base/apple/scoped_cftyperef.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/run_loop.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/sys_string_conversions.h"
#include "base/task/sequenced_task_runner.h"
#include "base/test/bind.h"
#include "base/test/run_until.h"
#include "base/test/test_future.h"
#include "base/threading/thread_restrictions.h"
#include "base/time/time.h"
#include "chrome/browser/headless/test/headless_mode_browsertest.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/search_engines/template_url_service_factory.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/browser/ui/navigator/browser_navigator_params.h"
#include "chrome/browser/ui/tabs/tab_enums.h"
#include "chrome/browser/ui/tabs/tab_group_model.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/test/base/search_test_utils.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/prefs/pref_service.h"
#include "components/search_engines/template_url.h"
#include "components/search_engines/template_url_data.h"
#include "components/search_engines/template_url_service.h"
#include "components/tabs/public/tab_group.h"
#include "content/public/browser/render_widget_host_view.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "net/test/embedded_test_server/http_request.h"
#include "net/test/embedded_test_server/http_response.h"
#include "ui/base/clipboard/clipboard.h"
#include "ui/base/page_transition_types.h"
#include "ui/events/keycodes/keyboard_codes.h"
#import "ui/events/test/cocoa_test_event_utils.h"
#include "ui/gfx/switches.h"
#include "ui/views/widget/widget.h"
#include "uw/browser/agent/chat_tab.h"
#include "uw/browser/agent/skill_folders.h"
#include "uw/browser/tab_organization/ai_connection.h"
#include "uw/browser/tab_organization/tab_organization_controller.h"
#include "uw/browser/tab_organization/window_screenshot.h"
#include "uw/browser/terminal/terminal_tab.h"
#include "uw/browser/ui/webui/uw_command_bar/exchange_rates.h"
#include "uw/browser/workspaces/workspace_theme.h"
#include "uw/components/look/theme.h"

namespace uw {
namespace {

class CommandBarBrowserTest : public headless::HeadlessModeBrowserTest {
 protected:
  void SetUpOnMainThread() override {
    HeadlessModeBrowserTest::SetUpOnMainThread();
    // A chat the bar opens lists skills: none from the user's own folder.
    SetAgentsSkillsFolderForTesting(
        browser()->GetProfile()->GetPath().AppendASCII("agents-skills"));
  }

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

  // A key as the page's own key handling sees it, pressed in the field.
  // `with` names a modifier, such as "metaKey", held with it.
  void PressInField(std::string_view key, std::string_view with = "") {
    ASSERT_TRUE(content::ExecJs(
        bar()->page(),
        content::JsReplace("document.querySelector('#field').dispatchEvent("
                           "new KeyboardEvent('keydown', {key: $1, [$2]: "
                           "true, bubbles: true}))",
                           key, with.empty() ? "none" : with)));
  }

  std::string FieldAndSelection() {
    return content::EvalJs(bar()->page(),
                           "(f => `${f.value}|${f.selectionStart}-"
                           "${f.selectionEnd}`)("
                           "document.querySelector('#field'))")
        .ExtractString();
  }

  // The field's text as drawn: what was typed, a bar, then what uw filled in,
  // which is muted.
  std::string Ink() {
    return content::EvalJs(bar()->page(), R"(
        (ink => {
          const filled = ink.querySelector('.filled').textContent;
          return ink.textContent.slice(0, -filled.length || undefined) + '|' +
              filled;
        })(document.querySelector('#ink'))
    )")
        .ExtractString();
  }

  // Visits `url` twice as if its address were typed: Chromium completes an
  // address with a path once it was typed twice.
  void VisitTyped(const GURL& url) {
    for (int i = 0; i < 2; ++i) {
      NavigateParams params(browser(), url, ui::PAGE_TRANSITION_TYPED);
      ui_test_utils::NavigateToURL(&params);
    }
  }

  // A file a test command writes, and its text once it is there.
  base::FilePath Marker() {
    base::ScopedAllowBlockingForTesting allow_blocking;
    if (!folder_.IsValid()) {
      EXPECT_TRUE(folder_.CreateUniqueTempDir());
    }
    return folder_.GetPath().AppendASCII("ran");
  }
  std::string MarkerText() {
    base::ScopedAllowBlockingForTesting allow_blocking;
    std::string text;
    base::ReadFileToString(Marker(), &text);
    return text;
  }

  base::ScopedTempDir folder_;

  // Each row's kind label, in order.
  std::string Kinds() {
    return content::EvalJs(bar()->page(),
                           "[...document.querySelectorAll('#results .kind')]"
                           ".map(kind => kind.textContent).join(',')")
        .ExtractString();
  }

  // Closes the panel as `reason` does: kLostFocus for a click elsewhere.
  void CloseBar(views::Widget::ClosedReason reason) {
    views::Widget::GetTopLevelWidgetForNativeView(
        bar()->page()->GetNativeView())
        ->CloseWithReason(reason);
    ASSERT_TRUE(Eventually([&] { return !bar()->IsShowing(); }));
  }

  // The shell or site search chip's text, or "" when it is hidden.
  std::string Chip() {
    return content::EvalJs(bar()->page(),
                           "(c => c.hidden ? '' : c.textContent)("
                           "document.querySelector('#chip'))")
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

  // The calculator card's sum, answer, and answer's name, or "" without one.
  std::string Card() {
    return content::EvalJs(bar()->page(),
                           "document.querySelector('#calc').hidden ? '' : "
                           "['#sum', '#answer', '#answerName']"
                           ".map(id => document.querySelector(id).textContent)"
                           ".join('|')")
        .ExtractString();
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

// It opens over the page, so it takes the page mode, per T198.
IN_PROC_BROWSER_TEST_F(CommandBarBrowserTest, ItTakesThePageMode) {
  Theme theme;
  theme.mode = ThemeMode::kLight;
  theme.page_mode = PageMode::kDark;
  SetThemeOfWindow(browser(), theme);

  Press(ui::VKEY_T, NSEventModifierFlagCommand);
  ASSERT_TRUE(bar()->IsShowing());
  EXPECT_TRUE(base::test::RunUntil([&] {
    return content::EvalJs(bar()->page(),
                           "matchMedia('(prefers-color-scheme: dark)').matches")
        .ExtractBool();
  }));
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

    // The first Escape empties the field, and the second closes the panel.
    Queue(ui::VKEY_ESCAPE, 0);
    EXPECT_TRUE(Eventually([&] { return Field().empty(); }));
    EXPECT_TRUE(bar()->IsShowing());
    Queue(ui::VKEY_ESCAPE, 0);
    ASSERT_TRUE(Eventually([&] { return !bar()->IsShowing(); }));
  }
}

// A click elsewhere closes it and keeps the text, caret, and card for the next
// time it opens, from Cmd+T or a task's "+", per T213.
IN_PROC_BROWSER_TEST_F(CommandBarBrowserTest, ALostFocusKeepsTheText) {
  ASSERT_TRUE(content::WaitForLoadStop(bar()->page()));
  Press(ui::VKEY_T, NSEventModifierFlagCommand);
  Type("2*(3+4)");
  ASSERT_TRUE(content::ExecJs(
      bar()->page(),
      "document.querySelector('#field').setSelectionRange(2, 2)"));
  const std::string card = Card();
  ASSERT_NE("", card);

  CloseBar(views::Widget::ClosedReason::kLostFocus);
  Press(ui::VKEY_T, NSEventModifierFlagCommand);
  ASSERT_TRUE(bar()->IsShowing());
  Wait(base::Milliseconds(200));
  EXPECT_EQ("2*(3+4)|2-2", FieldAndSelection());
  EXPECT_EQ(card, Card());

  CloseBar(views::Widget::ClosedReason::kLostFocus);
  ASSERT_TRUE(CommandBar::OpenForTask(browser(),
                                      tab_groups::TabGroupId::GenerateNew()));
  Wait(base::Milliseconds(200));
  EXPECT_EQ("2*(3+4)|2-2", FieldAndSelection());
}

// Any other close forgets the text: here a copied answer, which closes it as a
// picked row does.
IN_PROC_BROWSER_TEST_F(CommandBarBrowserTest, APickForgetsTheText) {
  ASSERT_TRUE(content::WaitForLoadStop(bar()->page()));
  Press(ui::VKEY_T, NSEventModifierFlagCommand);
  Type("2*(3+4)");
  PressInField("Enter");
  ASSERT_TRUE(Eventually([&] { return !bar()->IsShowing(); }));

  Press(ui::VKEY_T, NSEventModifierFlagCommand);
  EXPECT_TRUE(Eventually([&] { return Field().empty() && Card().empty(); }));
}

IN_PROC_BROWSER_TEST_F(CommandBarBrowserTest, CmdShiftPStartsOverKeptText) {
  ASSERT_TRUE(content::WaitForLoadStop(bar()->page()));
  Press(ui::VKEY_T, NSEventModifierFlagCommand);
  Type("git");
  CloseBar(views::Widget::ClosedReason::kLostFocus);

  Press(ui::VKEY_P, NSEventModifierFlagCommand | NSEventModifierFlagShift);

  EXPECT_TRUE(Eventually([&] { return Chip() == ">" && Field().empty(); }));
}

// Escape empties the field, a shell chip too, and closes it once the field is
// empty. The next time, it opens empty.
IN_PROC_BROWSER_TEST_F(CommandBarBrowserTest, EscapeEmptiesTheFieldFirst) {
  ASSERT_TRUE(content::WaitForLoadStop(bar()->page()));
  Press(ui::VKEY_P, NSEventModifierFlagCommand | NSEventModifierFlagShift);
  ASSERT_TRUE(Eventually([&] { return Chip() == ">"; }));
  Type("ls");

  PressInField("Escape");
  EXPECT_EQ("", Field());
  EXPECT_EQ("", Chip());
  Wait(base::Milliseconds(200));
  EXPECT_TRUE(bar()->IsShowing());

  Type("git");
  CloseBar(views::Widget::ClosedReason::kLostFocus);
  Press(ui::VKEY_T, NSEventModifierFlagCommand);
  PressInField("Escape");
  PressInField("Escape");
  ASSERT_TRUE(Eventually([&] { return !bar()->IsShowing(); }));

  Press(ui::VKEY_T, NSEventModifierFlagCommand);
  EXPECT_TRUE(Eventually([&] { return Field().empty(); }));
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

// The command bar opened by the "+" on the row of a task holding the first
// tab, with a tab outside the task selected.
class CommandBarForATaskBrowserTest : public CommandBarBrowserTest {
 protected:
  void OpenForATask() {
    ASSERT_TRUE(content::WaitForLoadStop(bar()->page()));
    chrome::NewTab(browser(), NewTabTypes::kNewTabCommand);
    Settle();
    TabOrganizationController* controller =
        TabOrganizationController::From(browser());
    task_ = controller->MoveToNewTask(
        {*controller->IdFor(tabs()->GetTabAtIndex(0))}, u"Plan");
    Settle();
    ASSERT_FALSE(tabs()->GetActiveTab()->GetGroup());
    ASSERT_TRUE(CommandBar::OpenForTask(browser(), *task_));
  }

  // True once the selected tab passes `is` and sits last in the task, at its
  // top level.
  bool LandsAtTheTaskEnd(base::RepeatingCallback<bool(const GURL&)> is) {
    return Eventually(
        [&] {
          tabs::TabInterface* tab = tabs()->GetActiveTab();
          return is.Run(tab->GetContents()->GetVisibleURL()) &&
                 tab->GetGroup() == task_ &&
                 tabs()->GetIndexOfTab(tab) + 1 ==
                     static_cast<int>(tabs()
                                          ->group_model()
                                          ->GetTabGroup(*task_)
                                          ->ListTabs()
                                          .end()) &&
                 TabOrganizationController::From(browser())->DepthFor(tab) == 0;
        },
        base::Seconds(5));
  }

  TabStripModel* tabs() { return browser()->tab_strip_model(); }

  std::optional<tab_groups::TabGroupId> task_;
};

IN_PROC_BROWSER_TEST_F(CommandBarForATaskBrowserTest,
                       APickedPageOpensAtTheTaskEnd) {
  ASSERT_TRUE(embedded_test_server()->Start());
  OpenForATask();
  const GURL page = embedded_test_server()->GetURL("/title2.html");
  Type(page.spec());
  ASSERT_TRUE(Eventually([&] { return Rows() > 0; }));
  PressInField("Enter");

  EXPECT_TRUE(LandsAtTheTaskEnd(base::BindRepeating(
      [](const GURL& page, const GURL& url) { return url == page; }, page)));
  EXPECT_FALSE(bar()->IsShowing());
}

IN_PROC_BROWSER_TEST_F(CommandBarForATaskBrowserTest, AChatOpensAtTheTaskEnd) {
  OpenForATask();
  Type("what is on this page");
  PressInField("Enter", "metaKey");

  EXPECT_TRUE(LandsAtTheTaskEnd(base::BindRepeating(
      [](const GURL& url) { return url.host() == kChatHost; })));
}

IN_PROC_BROWSER_TEST_F(CommandBarForATaskBrowserTest,
                       ATerminalOpensAtTheTaskEnd) {
  OpenForATask();
  Type("printf ran");
  PressInField("Enter", "altKey");

  EXPECT_TRUE(LandsAtTheTaskEnd(base::BindRepeating(&IsTerminalUrl)));
}

IN_PROC_BROWSER_TEST_F(CommandBarForATaskBrowserTest,
                       CmdTMakesItOpenWhereCmdTDoes) {
  ASSERT_TRUE(embedded_test_server()->Start());
  OpenForATask();
  // Cmd+T while it is still open for the task.
  Press(ui::VKEY_T, NSEventModifierFlagCommand);
  const GURL page = embedded_test_server()->GetURL("/title2.html");
  Type(page.spec());
  ASSERT_TRUE(Eventually([&] { return Rows() > 0; }));
  PressInField("Enter");

  EXPECT_TRUE(Eventually(
      [&] { return tabs()->GetActiveWebContents()->GetVisibleURL() == page; }));
  EXPECT_FALSE(tabs()->GetActiveTab()->GetGroup());
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

IN_PROC_BROWSER_TEST_F(CommandBarBrowserTest, ReturnCopiesTheAnswerOfASum) {
  ASSERT_TRUE(content::WaitForLoadStop(bar()->page()));
  Press(ui::VKEY_T, NSEventModifierFlagCommand);
  Type("20 kg * 10 m/s/s / 5W");
  EXPECT_EQ("20 kg × 10 m ÷ s ÷ s ÷ 5 W|40 s/m|Seconds per meter", Card());
  EXPECT_EQ(true, content::EvalJs(bar()->page(), "document.querySelector("
                                                 "'#calc').classList.contains("
                                                 "'selected')"));

  PressInField("Enter");
  EXPECT_TRUE(Eventually([&] { return !bar()->IsShowing(); }));
  base::test::TestFuture<std::u16string> copied;
  ui::Clipboard::GetForCurrentThread()->ReadText(
      ui::ClipboardBuffer::kCopyPaste, /*data_dst=*/std::nullopt,
      copied.GetCallback());
  EXPECT_EQ(u"40 s/m", copied.Get());
  EXPECT_EQ(1, browser()->tab_strip_model()->count());
}

IN_PROC_BROWSER_TEST_F(CommandBarBrowserTest, AWordThatIsNotAUnitHasNoAnswer) {
  ASSERT_TRUE(content::WaitForLoadStop(bar()->page()));
  Press(ui::VKEY_T, NSEventModifierFlagCommand);
  Type("1/2 cup flour");
  EXPECT_EQ("", Card());
  Type("covid-19");
  EXPECT_EQ("", Card());
  Type("10 kg + 5 m");
  EXPECT_EQ("10 kg + 5 m|Cannot add a mass to a length|No answer", Card());
  EXPECT_EQ(false, content::EvalJs(bar()->page(), "document.querySelector("
                                                  "'#calc').classList.contains("
                                                  "'selected')"));
}

IN_PROC_BROWSER_TEST_F(CommandBarBrowserTest, TimeMathLeavesMathAlone) {
  ASSERT_TRUE(content::WaitForLoadStop(bar()->page()));
  Press(ui::VKEY_T, NSEventModifierFlagCommand);
  Type("2026-12-25");
  EXPECT_TRUE(base::StartsWith(Card(), "Dec 25, 2026|Friday, Dec 25, 2026|"))
      << Card();
  EXPECT_EQ("date",
            content::EvalJs(bar()->page(), "document.querySelector('#answer')"
                                           ".className"));
  Type("3/4");
  EXPECT_EQ("3 ÷ 4|0.75|Result", Card());
  Type("now in Narnia");
  EXPECT_EQ("", Card());
  Type("feb 30");
  EXPECT_EQ("Feb 30|Feb 30 is not a date|No answer", Card());
}

// The pill under a time names the Mac's zone, which the page asks the browser
// for.
IN_PROC_BROWSER_TEST_F(CommandBarBrowserTest, ATimeIsReadInTheMacsZone) {
  base::apple::ScopedCFTypeRef<CFTimeZoneRef> zone(CFTimeZoneCopySystem());
  std::string name = base::SysCFStringRefToUTF8(CFTimeZoneGetName(zone.get()));
  name = name.substr(name.rfind('/') + 1);
  std::replace(name.begin(), name.end(), '_', ' ');
  ASSERT_TRUE(content::WaitForLoadStop(bar()->page()));
  Press(ui::VKEY_T, NSEventModifierFlagCommand);
  ASSERT_TRUE(Eventually([&] {
    Type("3pm");
    return content::EvalJs(bar()->page(),
                           "document.querySelector('#sumName').textContent")
               .ExtractString() == name;
  }));
}

// The page's clock is moved to half a second before a minute starts, so the
// card for `now +` shows the next minute without retyping.
IN_PROC_BROWSER_TEST_F(CommandBarBrowserTest, ACardBuiltOnNowKeepsTime) {
  ASSERT_TRUE(content::WaitForLoadStop(bar()->page()));
  ASSERT_TRUE(content::ExecJs(bar()->page(), R"(
      const start = performance.now();
      const minute = Math.ceil(Date.now() / 60000) * 60000 - 500;
      Date.now = () => minute + (performance.now() - start);
  )"));
  Press(ui::VKEY_T, NSEventModifierFlagCommand);
  Type("now +");
  const std::string first = Card();
  ASSERT_FALSE(first.empty());
  EXPECT_TRUE(Eventually([&] { return Card() != first; }, base::Seconds(3)))
      << first;
  EXPECT_EQ(true, content::EvalJs(bar()->page(), "document.querySelector("
                                                 "'#calc').classList.contains("
                                                 "'selected')"));
}

// Serves the European Central Bank's daily file with two rates, from before
// the command bar's page first asks for it.
class CommandBarRatesBrowserTest : public CommandBarBrowserTest {
 protected:
  void SetUp() override {
    embedded_test_server()->RegisterRequestHandler(base::BindRepeating(
        [](const net::test_server::HttpRequest& request)
            -> std::unique_ptr<net::test_server::HttpResponse> {
          if (request.relative_url != "/eurofxref-daily.xml") {
            return nullptr;
          }
          auto response =
              std::make_unique<net::test_server::BasicHttpResponse>();
          response->set_content_type("text/xml");
          response->set_content(R"(<?xml version="1.0" encoding="UTF-8"?>
<gesmes:Envelope xmlns:gesmes="http://www.gesmes.org/xml/2002-08-01"
    xmlns="http://www.ecb.int/vocabulary/2002-08-01/eurofxref">
  <gesmes:subject>Reference rates</gesmes:subject>
  <gesmes:Sender><gesmes:name>European Central Bank</gesmes:name></gesmes:Sender>
  <Cube>
    <Cube time="2026-10-15">
      <Cube currency="USD" rate="1.0800"/>
      <Cube currency="HKD" rate="8.4000"/>
    </Cube>
  </Cube>
</gesmes:Envelope>)");
          return response;
        }));
    ASSERT_TRUE(embedded_test_server()->InitializeAndListen());
    ExchangeRates::SetSourceForTesting(
        embedded_test_server()->GetURL("/eurofxref-daily.xml"));
    CommandBarBrowserTest::SetUp();
  }

  void SetUpOnMainThread() override {
    CommandBarBrowserTest::SetUpOnMainThread();
    embedded_test_server()->StartAcceptingConnections();
  }
};

IN_PROC_BROWSER_TEST_F(CommandBarRatesBrowserTest,
                       ASumOfCurrenciesUsesTheDaysRates) {
  PrefService* prefs = browser()->GetProfile()->GetPrefs();
  prefs->SetString(kCommandBarCurrencyPref, "HKD");
  ASSERT_TRUE(content::WaitForLoadStop(bar()->page()));
  // The page asks again each time the panel shows.
  ASSERT_TRUE(Eventually(
      [&] {
        EXPECT_TRUE(content::ExecJs(
            bar()->page(), "window.dispatchEvent(new Event('focus'))"));
        return prefs->GetDict(kCommandBarRatesPref).FindDict("per_euro");
      },
      base::Seconds(10)));
  ASSERT_TRUE(content::ExecJs(bar()->page(),
                              "window.dispatchEvent(new Event('focus'))"));

  Press(ui::VKEY_T, NSEventModifierFlagCommand);
  ASSERT_TRUE(Eventually([&] {
    Type("10 HKD + 10 EUR to USD");
    return !Card().empty();
  }));
  EXPECT_EQ("10 HKD + 10 EUR|$12.09|US dollars", Card());
  EXPECT_EQ("HK$10 = $1.29 · €10 = $10.80 · ECB rates of Oct 15, 2026",
            content::EvalJs(bar()->page(),
                            "document.querySelector('#calcNote').textContent"));
  Type("100 usd");
  EXPECT_EQ("100 USD|HK$777.78|Hong Kong dollars", Card());
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

  Type(">");
  Type("ls -la ~/dev");
  Wait(base::Milliseconds(300));
  SaveWidgetScreenshot(views::Widget::GetTopLevelWidgetForNativeView(
                           bar()->page()->GetNativeView()),
                       "command-bar-shell");

  Type("");
  PressInField("Backspace");
  Type("@");
  ASSERT_TRUE(Eventually([&] { return !Chip().empty(); }));
  Type("what is this page about");
  Wait(base::Milliseconds(300));
  SaveWidgetScreenshot(views::Widget::GetTopLevelWidgetForNativeView(
                           bar()->page()->GetNativeView()),
                       "command-bar-page");

  Type("");
  PressInField("Backspace");
  for (auto [text, name] :
       {std::pair{"20 kg * 10 m/s/s / 5W", "command-bar-calculator"},
        std::pair{"10 kg + 5 m", "command-bar-no-answer"},
        std::pair{"now + 100 minutes to UTC", "command-bar-time"},
        std::pair{"dec 25", "command-bar-date"}}) {
    Type(text);
    Wait(base::Milliseconds(300));
    SaveWidgetScreenshot(views::Widget::GetTopLevelWidgetForNativeView(
                             bar()->page()->GetNativeView()),
                         name);
  }
}

IN_PROC_BROWSER_TEST_F(CommandBarBrowserTest, CmdReturnAsksInANewChat) {
  ASSERT_TRUE(content::WaitForLoadStop(bar()->page()));
  Press(ui::VKEY_T, NSEventModifierFlagCommand);
  Type("what is on this page");
  PressInField("Enter", "metaKey");

  TabStripModel* tabs = browser()->tab_strip_model();
  EXPECT_TRUE(Eventually(
      [&] {
        return tabs->GetActiveWebContents()->GetVisibleURL().host() ==
                   kChatHost &&
               tabs->GetActiveWebContents()->GetTitle() ==
                   u"what is on this page";
      },
      base::Seconds(5)));
  EXPECT_FALSE(bar()->IsShowing());
}

IN_PROC_BROWSER_TEST_F(CommandBarBrowserTest,
                       OptionReturnRunsTheTextInANewTerminal) {
  ASSERT_TRUE(content::WaitForLoadStop(bar()->page()));
  const int count = browser()->tab_strip_model()->count();
  Press(ui::VKEY_T, NSEventModifierFlagCommand);
  Type("printf ran > '" + Marker().value() + "'");
  PressInField("Enter", "altKey");

  EXPECT_TRUE(
      Eventually([&] { return MarkerText() == "ran"; }, base::Seconds(10)));
  EXPECT_EQ(count + 1, browser()->tab_strip_model()->count());
  EXPECT_TRUE(IsTerminalUrl(
      browser()->tab_strip_model()->GetActiveWebContents()->GetVisibleURL()));
}

IN_PROC_BROWSER_TEST_F(CommandBarBrowserTest, AShellCommandRunsOnReturn) {
  ASSERT_TRUE(content::WaitForLoadStop(bar()->page()));
  Press(ui::VKEY_T, NSEventModifierFlagCommand);
  Type(">");
  EXPECT_EQ(">",
            content::EvalJs(bar()->page(),
                            "document.querySelector('#chip').textContent"));
  Type("printf ran > '" + Marker().value() + "'");
  EXPECT_EQ(0, Rows());
  PressInField("Enter");

  EXPECT_TRUE(
      Eventually([&] { return MarkerText() == "ran"; }, base::Seconds(10)));
}

// A "@" in an empty field turns into the chip of the web page behind the
// command bar, and the text no longer lists rows. Backspace in the empty field
// takes the chip away. Off a web page, the "@" stays typed text.
IN_PROC_BROWSER_TEST_F(CommandBarBrowserTest, AnAtTakesThePageBehindIt) {
  ASSERT_TRUE(embedded_test_server()->Start());
  ASSERT_TRUE(content::WaitForLoadStop(bar()->page()));
  Press(ui::VKEY_T, NSEventModifierFlagCommand);
  Type("@");
  Settle();
  EXPECT_EQ("", Chip());
  EXPECT_EQ("@", Field());
  PressInField("Escape");

  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("/title2.html")));
  Type("@");
  ASSERT_TRUE(Eventually([&] { return Chip() == "Title Of Awesomeness"; }));
  EXPECT_EQ("", Field());
  Type("what is this");
  Settle();
  EXPECT_EQ(0, Rows());

  Type("");
  PressInField("Backspace");
  EXPECT_EQ("", Chip());
}

IN_PROC_BROWSER_TEST_F(CommandBarBrowserTest,
                       CmdShiftPOpensItForAShellCommand) {
  ASSERT_TRUE(content::WaitForLoadStop(bar()->page()));
  Press(ui::VKEY_P, NSEventModifierFlagCommand | NSEventModifierFlagShift);

  EXPECT_TRUE(bar()->IsShowing());
  EXPECT_TRUE(Eventually([&] {
    return content::EvalJs(bar()->page(),
                           "(c => c.hidden ? '' : c.textContent)("
                           "document.querySelector('#chip'))") == ">";
  }));
}

IN_PROC_BROWSER_TEST_F(CommandBarBrowserTest, TabTurnsTheCompletionIntoText) {
  ASSERT_TRUE(embedded_test_server()->Start());
  const GURL page = embedded_test_server()->GetURL("/title2.html");
  VisitTyped(page);
  ASSERT_TRUE(content::WaitForLoadStop(bar()->page()));
  Press(ui::VKEY_T, NSEventModifierFlagCommand);

  // Typed as people type addresses, without the scheme.
  const std::string start =
      base::StrCat({page.host(), ":", page.port(), "/titl"});
  const std::string whole = start + "e2.html";
  const std::string size = base::NumberToString(whole.size());
  ASSERT_TRUE(Eventually(
      [&] {
        Type(start);
        Wait(base::Milliseconds(100));
        return Ink() == start + "|e2.html";
      },
      base::Seconds(5)))
      << Ink();
  PressInField("Tab");
  EXPECT_EQ(whole + "|" + size + "-" + size, FieldAndSelection());
  EXPECT_EQ(whole + "|", Ink());
}

// The completion is muted after the caret and is not in the field, so typing
// and Backspace work on what was typed.
IN_PROC_BROWSER_TEST_F(CommandBarBrowserTest,
                       TheCompletionIsMutedAfterTheCaret) {
  ASSERT_TRUE(embedded_test_server()->Start());
  const GURL page = embedded_test_server()->GetURL("/title2.html");
  VisitTyped(page);
  ASSERT_TRUE(content::WaitForLoadStop(bar()->page()));
  Press(ui::VKEY_T, NSEventModifierFlagCommand);

  const std::string start =
      base::StrCat({page.host(), ":", page.port(), "/titl"});
  const std::string caret = base::NumberToString(start.size());
  const auto completed = [&] {
    return Eventually(
        [&] {
          // Typed again from empty, so the caret is at the end.
          Type("");
          Type(start);
          Wait(base::Milliseconds(100));
          return Ink() == start + "|e2.html";
        },
        base::Seconds(5));
  };
  ASSERT_TRUE(completed()) << Ink();
  EXPECT_EQ(start + "|" + caret + "-" + caret, FieldAndSelection());

  // Typing the completion's next letter keeps the rest before the rows come.
  Type(start + "e");
  EXPECT_EQ(start + "e|2.html", Ink());

  // Backspace deletes a typed letter, and the completion goes with it.
  ASSERT_TRUE(content::ExecJs(bar()->page(), content::JsReplace(R"(
      const field = document.querySelector('#field');
      field.value = $1;
      field.dispatchEvent(
          new InputEvent('input', {inputType: 'deleteContentBackward'}));
  )",
                                                                start)));
  EXPECT_EQ(start + "|", Ink());
  Wait(base::Milliseconds(300));
  EXPECT_EQ(start + "|", Ink());

  // Moving the caret drops the completion.
  ASSERT_TRUE(completed()) << Ink();
  ASSERT_TRUE(content::ExecJs(
      bar()->page(),
      "document.querySelector('#field').setSelectionRange(0, 0)"));
  EXPECT_TRUE(Eventually([&] { return Ink() == start + "|"; })) << Ink();

  // → takes it.
  ASSERT_TRUE(completed()) << Ink();
  PressInField("ArrowRight");
  const std::string whole = start + "e2.html";
  const std::string size = base::NumberToString(whole.size());
  EXPECT_EQ(whole + "|" + size + "-" + size, FieldAndSelection());
  EXPECT_EQ(whole + "|", Ink());
}

// The arrow keys put a row's address in the field, muted until it is edited.
// Going back to the first row brings back what was typed.
IN_PROC_BROWSER_TEST_F(CommandBarBrowserTest,
                       TheArrowKeysPutARowsAddressInTheField) {
  ASSERT_TRUE(embedded_test_server()->Start());
  const GURL page = embedded_test_server()->GetURL("/title2.html");
  VisitTyped(page);
  ASSERT_TRUE(content::WaitForLoadStop(bar()->page()));
  Press(ui::VKEY_T, NSEventModifierFlagCommand);

  // The page's title, "Title Of Awesomeness", finds it.
  ASSERT_TRUE(Eventually(
      [&] {
        Type("awesomeness");
        Wait(base::Milliseconds(100));
        return Kinds().find("Search,") == 0 && Rows() > 1;
      },
      base::Seconds(5)))
      << "The rows are: " << Kinds();
  const std::string address =
      base::StrCat({page.host(), ":", page.port(), "/title2.html"});
  const std::string size = base::NumberToString(address.size());

  PressInField("ArrowDown");
  EXPECT_EQ(address + "|" + size + "-" + size, FieldAndSelection());
  EXPECT_EQ("|" + address, Ink());

  PressInField("ArrowUp");
  EXPECT_EQ("awesomeness|11-11", FieldAndSelection());
  EXPECT_EQ("awesomeness|", Ink());

  PressInField("ArrowDown");
  Type(address + "?q");
  EXPECT_EQ(address + "?q|", Ink());
}

// The look of a completion and of a long address put in the field: run in the
// VM with UW_SCREENSHOT_DIR set.
IN_PROC_BROWSER_TEST_F(CommandBarBrowserTest, MANUAL_ARowsAddressInTheField) {
  ASSERT_TRUE(embedded_test_server()->Start());
  const GURL page = embedded_test_server()->GetURL("/title2.html");
  VisitTyped(page);
  VisitTyped(embedded_test_server()->GetURL(
      "/title2.html?from=the-command-bar&to=a-field-too-narrow-for-it"));
  ASSERT_TRUE(content::WaitForLoadStop(bar()->page()));
  Press(ui::VKEY_T, NSEventModifierFlagCommand);
  views::Widget* panel = views::Widget::GetTopLevelWidgetForNativeView(
      bar()->page()->GetNativeView());

  const std::string start =
      base::StrCat({page.host(), ":", page.port(), "/ti"});
  ASSERT_TRUE(Eventually(
      [&] {
        Type(start);
        Wait(base::Milliseconds(100));
        return Ink().size() > start.size() + 1 && Rows() > 1;
      },
      base::Seconds(5)))
      << Ink();
  Wait(base::Milliseconds(300));
  SaveWidgetScreenshot(panel, "command-bar-completion");
  // The caret shows only in a focused field.
  EXPECT_EQ("field",
            content::EvalJs(bar()->page(), "document.hasFocus() && "
                                           "document.activeElement.id"));

  for (int row = 1; row < Rows() && Field().find("narrow") == std::string::npos;
       ++row) {
    PressInField("ArrowDown");
  }
  ASSERT_NE(std::string::npos, Field().find("narrow")) << Field();
  Wait(base::Milliseconds(300));
  SaveWidgetScreenshot(panel, "command-bar-long-address");

  // The field's own text, drawn red, lies over its ink.
  ASSERT_TRUE(content::ExecJs(
      bar()->page(),
      "document.querySelector('#field').style.color = 'rgb(255 0 0 / 0.6)'"));
  Wait(base::Milliseconds(300));
  SaveWidgetScreenshot(panel, "command-bar-field-over-ink");

  // The field scrolled to the caret at the end, and its ink with it.
  const std::string scrolled =
      content::EvalJs(bar()->page(),
                      "`${document.querySelector('#field').scrollLeft}|"
                      "${document.querySelector('#ink').scrollLeft}`")
          .ExtractString();
  const std::string field_scroll = scrolled.substr(0, scrolled.find('|'));
  EXPECT_NE("0", field_scroll);
  EXPECT_EQ(field_scroll + "|" + field_scroll, scrolled);
}

// Return opens the completed address, even when pressed before the rows for
// the last letter arrived.
IN_PROC_BROWSER_TEST_F(CommandBarBrowserTest, ReturnOpensTheCompletedAddress) {
  ASSERT_TRUE(embedded_test_server()->Start());
  const GURL page = embedded_test_server()->GetURL("/title2.html");
  VisitTyped(page);
  ASSERT_TRUE(content::WaitForLoadStop(bar()->page()));
  TabStripModel* tabs = browser()->tab_strip_model();
  const int count = tabs->count();
  Press(ui::VKEY_T, NSEventModifierFlagCommand);

  const std::string start =
      base::StrCat({page.host(), ":", page.port(), "/titl"});
  // History completes the typed visit once it has it in memory.
  ASSERT_TRUE(Eventually(
      [&] {
        Type(start);
        Wait(base::Milliseconds(100));
        return Ink() == start + "|e2.html";
      },
      base::Seconds(5)));
  Type(start);
  PressInField("Enter");

  EXPECT_TRUE(Eventually([&] {
    return tabs->count() == count + 1 &&
           tabs->GetActiveWebContents()->GetVisibleURL() == page;
  }));
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

// A Retina main screen, and a 1x display beside it.
class CommandBarTwoScreensBrowserTest : public CommandBarBrowserTest {
 protected:
  void SetUpCommandLine(base::CommandLine* command_line) override {
    CommandBarBrowserTest::SetUpCommandLine(command_line);
    command_line->AppendSwitchASCII(
        switches::kScreenInfo, "{1600x1200 devicePixelRatio=2}{1600x1200}");
  }

  std::string PanelSize() {
    return bar()
        ->page()
        ->GetRenderWidgetHostView()
        ->GetViewBounds()
        .size()
        .ToString();
  }

  std::string PageSize() {
    return content::EvalJs(
               bar()->page(),
               "(r => `${Math.ceil(r.width)}x${Math.ceil(r.height)}`)("
               "document.documentElement.getBoundingClientRect())")
        .ExtractString();
  }
};

// The panel's window is made on the main screen before it moves over the
// browser window. On the 1x display, the panel still opens at its page's size.
IN_PROC_BROWSER_TEST_F(CommandBarTwoScreensBrowserTest,
                       ThePanelOpensAtItsPagesSize) {
  browser()->GetWindow()->SetBounds(gfx::Rect(900, 50, 1200, 800));
  ASSERT_TRUE(content::WaitForLoadStop(bar()->page()));

  for (int open = 1; open <= 2; ++open) {
    Press(ui::VKEY_T, NSEventModifierFlagCommand);
    ASSERT_TRUE(bar()->IsShowing());
    EXPECT_EQ(PageSize(), PanelSize()) << "Open " << open;

    ASSERT_TRUE(Eventually([&] {
      return content::EvalJs(bar()->page(), "devicePixelRatio") == 1;
    }));
    views::Widget::GetTopLevelWidgetForNativeView(
        bar()->page()->GetNativeView())
        ->Close();
    ASSERT_TRUE(Eventually([&] { return !bar()->IsShowing(); }));
  }
}

}  // namespace
}  // namespace uw
