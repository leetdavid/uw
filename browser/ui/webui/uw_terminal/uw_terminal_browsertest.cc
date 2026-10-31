// Copyright 2026 The uw Authors. All rights reserved.

#include <errno.h>
#include <signal.h>

#include <functional>
#include <optional>
#include <string>
#include <string_view>

#include "base/environment.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/run_loop.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/test/bind.h"
#include "base/test/run_until.h"
#include "base/threading/thread_restrictions.h"
#include "chrome/app/chrome_command_ids.h"
#include "chrome/browser/headless/test/headless_mode_browsertest.h"
#include "chrome/browser/lifetime/application_lifetime_desktop.h"
#include "chrome/browser/lifetime/browser_shutdown.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/tab_contents/chrome_web_contents_view_handle_drop.h"
#include "chrome/browser/ui/tabs/tab_group_model.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/test/base/ui_test_utils.h"
#include "content/public/browser/render_widget_host.h"
#include "content/public/browser/render_widget_host_view.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/drop_data.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "third_party/blink/public/common/input/web_mouse_event.h"
#include "third_party/blink/public/common/input/web_mouse_wheel_event.h"
#include "third_party/blink/public/common/page/drag_operation.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "ui/base/clipboard/clipboard.h"
#include "ui/base/clipboard/clipboard_buffer.h"
#include "ui/base/clipboard/file_info.h"
#include "ui/base/clipboard/scoped_clipboard_writer.h"
#include "ui/base/clipboard/test/test_clipboard.h"
#include "ui/events/keycodes/dom/dom_code.h"
#include "ui/events/keycodes/dom/dom_key.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/gfx/geometry/point.h"
#include "ui/gfx/geometry/point_f.h"
#include "ui/gfx/geometry/vector2d.h"
#include "ui/views/controls/label.h"
#include "ui/views/view_utils.h"
#include "ui/views/widget/widget.h"
#include "ui/views/window/dialog_delegate.h"
#include "url/gurl.h"
#include "uw/browser/tab_organization/commands.h"
#include "uw/browser/tab_organization/tab_organization_controller.h"
#include "uw/browser/tab_organization/window_screenshot.h"
#include "uw/browser/terminal/terminal_shell.h"
#include "uw/browser/terminal/terminal_tab.h"
#include "uw/browser/ui/views/tab_organization/key_hint.h"
#include "uw/browser/ui/views/terminal/close_warning.h"
#include "uw/components/terminal/shell_quote.h"

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
    // Pastes read a test clipboard, so the tests leave the user's alone.
    ui::TestClipboard::CreateForCurrentThread();
  }

  void TearDownOnMainThread() override {
    ui::Clipboard::DestroyClipboardForCurrentThread();
    HeadlessModeBrowserTest::TearDownOnMainThread();
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

  // Opens a terminal running `cat`, which writes each byte it gets to the
  // file `name` at once.
  content::WebContents* OpenCat(std::string_view name) {
    content::WebContents* terminal = OpenTerminal();
    Type(u"cd " + Folder() + u" && stty -icanon && cat > " +
         base::UTF8ToUTF16(name) + u"\r");
    WaitForFile(name, [](const std::string&) { return true; });
    return terminal;
  }

  base::FilePath NewFile(std::string_view name) {
    base::ScopedAllowBlockingForTesting allow_blocking;
    const base::FilePath file = folder_path_.Append(name);
    EXPECT_TRUE(base::WriteFile(file, ""));
    return file;
  }

  SkBitmap Image() {
    SkBitmap image;
    image.allocN32Pixels(4, 4);
    image.eraseColor(SK_ColorRED);
    return image;
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

// Shift+Enter sends Esc and Enter, as Option+Enter does.
IN_PROC_BROWSER_TEST_F(UwTerminalBrowserTest, ShiftEnterSendsOptionEnter) {
  OpenTerminal();
  Type(u"cd " + Folder() + u" && cat -v > keys\r");
  // The file appears once cat has started.
  WaitForFile("keys", [](const std::string&) { return true; });
  Type(u"x");
  content::SimulateKeyPress(contents(), ui::DomKey::ENTER, ui::DomCode::ENTER,
                            ui::VKEY_RETURN, false, /*shift=*/true, false,
                            false);
  Press('d', ui::DomCode::US_D, ui::VKEY_D, /*control=*/true, false, false);
  EXPECT_EQ(
      WaitForFile("keys",
                  [](const std::string& text) { return text.ends_with('\n'); }),
      "x^[\n");
}

// A program that asks for the mouse, as a TUI does, gets each click.
IN_PROC_BROWSER_TEST_F(UwTerminalBrowserTest,
                       AProgramThatAsksForTheMouseGetsClicks) {
  content::WebContents* terminal = OpenTerminal();
  Type(u"cd " + Folder() +
       u" && printf '\\033[?1000h\\033[?1006h\\033]0;mouse\\007' && "
       u"cat -v > clicks\r");
  // The file appears once cat has started. The title follows the request for
  // the mouse, so it shows once the page has read it.
  WaitForFile("clicks", [](const std::string&) { return true; });
  EXPECT_TRUE(base::test::RunUntil([&] {
    return terminal->GetTitle() == u"mouse";
  })) << terminal->GetTitle();
  content::SimulateMouseClickAt(
      contents(), 0, blink::WebMouseEvent::Button::kLeft, gfx::Point(100, 60));
  Type(u"\r");
  Press('d', ui::DomCode::US_D, ui::VKEY_D, /*control=*/true, false, false);
  const std::string clicks = WaitForFile(
      "clicks", [](const std::string& text) { return text.ends_with('\n'); });
  // A press and a release, in the SGR form.
  EXPECT_TRUE(clicks.starts_with("^[[<0;")) << clicks;
  EXPECT_NE(clicks.find("M^[[<0;"), std::string::npos) << clicks;
  EXPECT_TRUE(clicks.ends_with("m\n")) << clicks;
}

// A Cmd-click is the browser's: the page opens the link, and a program that
// asks for the mouse never sees the click. Such a program gets other clicks,
// such as the Ctrl-click herdr opens links on, and the page opens no tab.
IN_PROC_BROWSER_TEST_F(UwTerminalBrowserTest,
                       ACmdClickOpensALinkThatOtherClicksLeaveToTheProgram) {
  content::WebContents* terminal = OpenTerminal();
  TabStripModel* tabs = browser()->tab_strip_model();
  const int count = tabs->count();
  Type(u"cd " + Folder() +
       u" && printf '\\033[2J\\033[Hhttps://example.com/link\\n"
       u"\\033[?1000h\\033[?1006h\\033]0;link\\007' && stty -icanon && "
       u"cat > clicks\r");
  // The file appears once cat has started. The title follows the request for
  // the mouse, so it shows once the page has read it.
  WaitForFile("clicks", [](const std::string&) { return true; });
  EXPECT_TRUE(base::test::RunUntil([&] {
    return terminal->GetTitle() == u"link";
  })) << terminal->GetTitle();
  // The link is in the first row.
  const gfx::Point link(40, 14);
  content::SimulateMouseClickAt(terminal, blink::WebInputEvent::kControlKey,
                                blink::WebMouseEvent::Button::kLeft, link);
  // Ctrl is reported as 16, in a press and a release.
  WaitForFile("clicks", [](const std::string& text) {
    return text.starts_with("\x1b[<16;") && text.ends_with('m');
  });
  EXPECT_EQ(tabs->count(), count);

  content::SimulateMouseClickAt(terminal, blink::WebInputEvent::kMetaKey,
                                blink::WebMouseEvent::Button::kLeft, link);
  EXPECT_TRUE(base::test::RunUntil([&] {
    for (int i = 0; i < tabs->count(); ++i) {
      if (tabs->GetWebContentsAt(i)->GetVisibleURL() ==
          GURL("https://example.com/link")) {
        return true;
      }
    }
    return false;
  }));
  tabs->ActivateTabAt(tabs->GetIndexOfWebContents(terminal));
  Type(u"x");
  const std::string clicks = WaitForFile(
      "clicks", [](const std::string& text) { return text.ends_with('x'); });
  EXPECT_EQ(clicks.find("\x1b[<8;"), std::string::npos) << clicks;
  EXPECT_EQ(tabs->count(), count + 1);
}

// A key hint: an address underlines, and the pointer becomes a hand, only
// while Cmd is held, since only a Cmd-click opens it.
IN_PROC_BROWSER_TEST_F(UwTerminalBrowserTest,
                       AnAddressUnderlinesOnlyWhileCmdIsHeld) {
  content::WebContents* terminal = OpenTerminal();
  Type(
      u"printf '\\033[2J\\033[Hhttps://example.com/link\\n"
      u"\\033]0;link\\007' && cat\r");
  EXPECT_TRUE(base::test::RunUntil([&] {
    return terminal->GetTitle() == u"link";
  })) << terminal->GetTitle();
  const auto hand = [&] {
    return content::EvalJs(terminal,
                           "document.body.classList.contains('cmd-link')")
        .ExtractBool();
  };
  const auto cmd = [&](bool held) {
    ASSERT_TRUE(content::ExecJs(
        terminal,
        base::StrCat({"window.dispatchEvent(new KeyboardEvent('",
                      held ? "keydown" : "keyup", "', {key: 'Meta', metaKey: ",
                      held ? "true" : "false", "}));"})));
  };
  const auto cursor = [&] {
    return content::EvalJs(
               terminal,
               "getComputedStyle(document.querySelector('#terminal canvas'))"
               "    .cursor")
        .ExtractString();
  };

  // The link is in the first row. The move has no Cmd, so it goes first.
  // ghostty-web sets its own pointer once it has found the address under it.
  content::SimulateMouseEvent(terminal, blink::WebInputEvent::Type::kMouseMove,
                              gfx::Point(40, 14));
  EXPECT_TRUE(base::test::RunUntil([&] {
    return content::EvalJs(terminal,
                           "document.querySelector('#terminal').style.cursor")
               .ExtractString() == "pointer";
  }));
  EXPECT_FALSE(hand());
  EXPECT_EQ(cursor(), "text");

  cmd(true);
  EXPECT_TRUE(base::test::RunUntil(hand));
  EXPECT_EQ(cursor(), "pointer");

  cmd(false);
  EXPECT_TRUE(base::test::RunUntil([&] { return !hand(); }));
  EXPECT_EQ(cursor(), "text");

  // Switching to another window lets go of the key.
  cmd(true);
  EXPECT_TRUE(base::test::RunUntil(hand));
  ASSERT_TRUE(
      content::ExecJs(terminal, "window.dispatchEvent(new Event('blur'));"));
  EXPECT_TRUE(base::test::RunUntil([&] { return !hand(); }));
}

// A program that asks for the mouse gets the wheel as mouse buttons, also on
// the alternate screen, where ghostty-web would send arrow keys.
IN_PROC_BROWSER_TEST_F(UwTerminalBrowserTest,
                       AProgramThatAsksForTheMouseGetsTheWheel) {
  content::WebContents* terminal = OpenTerminal();
  Type(u"cd " + Folder() +
       u" && stty -icanon && printf '\\033[?1049h\\033[?1000h\\033[?1006h"
       u"\\033]0;wheel\\007' && cat > wheel\r");
  // The file appears once cat has started. The title follows the request for
  // the mouse, so it shows once the page has read it.
  WaitForFile("wheel", [](const std::string&) { return true; });
  EXPECT_TRUE(base::test::RunUntil([&] {
    return terminal->GetTitle() == u"wheel";
  })) << terminal->GetTitle();
  content::SimulateMouseWheelEvent(contents(), gfx::Point(100, 60),
                                   gfx::Vector2d(0, 120),
                                   blink::WebMouseWheelEvent::kPhaseBegan);
  content::SimulateMouseWheelEvent(contents(), gfx::Point(100, 60),
                                   gfx::Vector2d(0, -120),
                                   blink::WebMouseWheelEvent::kPhaseChanged);
  // Up is button 64 and down is 65, in the SGR form.
  const std::string wheel = WaitForFile("wheel", [](const std::string& text) {
    return text.find("M\x1b[<65;") != std::string::npos;
  });
  EXPECT_TRUE(wheel.starts_with("\x1b[<64;")) << wheel;
  EXPECT_EQ(wheel.find("\x1b[A"), std::string::npos) << wheel;
}

IN_PROC_BROWSER_TEST_F(UwTerminalBrowserTest, APastedFileTypesItsPath) {
  const base::FilePath file = NewFile("my shot (1).png");
  {
    ui::ScopedClipboardWriter writer(ui::ClipboardBuffer::kCopyPaste);
    writer.WriteFilenames(
        ui::FileInfosToURIList({ui::FileInfo(file, file.BaseName())}));
  }
  OpenCat("pasted");
  contents()->Paste();
  const std::string quoted = QuotePathsForShell({file});
  EXPECT_EQ(WaitForFile("pasted",
                        [&](const std::string& text) {
                          return text.size() >= quoted.size();
                        }),
            quoted);
}

IN_PROC_BROWSER_TEST_F(UwTerminalBrowserTest,
                       APastedImageIsSavedAndTypesItsPath) {
  {
    ui::ScopedClipboardWriter writer(ui::ClipboardBuffer::kCopyPaste);
    writer.WriteImage(Image());
  }
  OpenCat("pasted");
  contents()->Paste();
  const base::FilePath image(WaitForFile("pasted", [](const std::string& text) {
    return text.ends_with("/image.png");
  }));
  base::ScopedAllowBlockingForTesting allow_blocking;
  std::string png;
  EXPECT_TRUE(base::ReadFileToString(image, &png));
  EXPECT_TRUE(png.starts_with("\x89PNG")) << image;
  base::DeletePathRecursively(image.DirName());
}

IN_PROC_BROWSER_TEST_F(UwTerminalBrowserTest, TextWithAnImagePastesTheText) {
  {
    ui::ScopedClipboardWriter writer(ui::ClipboardBuffer::kCopyPaste);
    writer.WriteText(u"caption");
    writer.WriteImage(Image());
  }
  OpenCat("pasted");
  contents()->Paste();
  EXPECT_EQ(
      WaitForFile("pasted",
                  [](const std::string& text) { return text.size() >= 7; }),
      "caption");
}

// The browser hands a terminal tab the paths of dropped files. Any other tab
// gets the files.
IN_PROC_BROWSER_TEST_F(UwTerminalBrowserTest, ADroppedFileTypesItsPath) {
  const base::FilePath file = NewFile("dropped file.txt");
  content::DropData drop;
  drop.filenames.emplace_back(file, file.BaseName());

  std::optional<content::DropData> page_drop;
  HandleOnPerformingDrop(
      browser()->tab_strip_model()->GetActiveWebContents(), drop,
      base::BindLambdaForTesting(
          [&](std::optional<content::DropData> data) { page_drop = data; }));
  ASSERT_TRUE(page_drop);
  EXPECT_EQ(page_drop->filenames.size(), 1u);

  content::WebContents* terminal = OpenCat("dropped");
  content::RenderWidgetHost* widget =
      terminal->GetRenderWidgetHostView()->GetRenderWidgetHost();
  const gfx::PointF point(100, 60);
  drop.view_id = widget->GetRoutingID();
  widget->DragTargetDragEnter(drop, point, point, blink::kDragOperationCopy, 0,
                              base::DoNothing());
  widget->DragTargetDragOver(point, point, blink::kDragOperationCopy, 0,
                             base::DoNothing());
  HandleOnPerformingDrop(
      terminal, drop,
      base::BindLambdaForTesting([&](std::optional<content::DropData> data) {
        ASSERT_TRUE(data);
        EXPECT_TRUE(data->filenames.empty());
        widget->DragTargetDrop(*data, point, point, 0, base::DoNothing());
      }));
  const std::string quoted = QuotePathsForShell({file});
  EXPECT_EQ(WaitForFile("dropped",
                        [&](const std::string& text) {
                          return text.size() >= quoted.size();
                        }),
            quoted);
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

// Clear closes a terminal at its prompt and leaves one running a command.
IN_PROC_BROWSER_TEST_F(UwTerminalBrowserTest,
                       ClearLeavesATerminalRunningACommand) {
  content::WebContents* idle = OpenTerminal();
  content::WebContents* busy = OpenTerminal();
  Type(u"sleep 60\r");
  TerminalShell* shell = TerminalShell::From(busy);
  ASSERT_TRUE(shell);
  bool asking = false;
  bool running = false;
  ASSERT_TRUE(base::test::RunUntil([&] {
    if (!asking) {
      asking = true;
      shell->GetActivity(
          base::BindLambdaForTesting([&](ShellHost::Activity activity) {
            running = activity.program == "sleep";
            asking = false;
          }));
    }
    return running;
  }));

  chrome::ExecuteCommand(browser(), kClearScratchpad);
  TabStripModel* tabs = browser()->tab_strip_model();
  EXPECT_TRUE(base::test::RunUntil([&] {
    return tabs->GetIndexOfWebContents(idle) == TabStripModel::kNoTab;
  }));
  EXPECT_NE(tabs->GetIndexOfWebContents(busy), TabStripModel::kNoTab);
}

// Closing a terminal that runs a program asks first, however it closes. See
// docs/plans/terminal-tab.md#busy-terminal-warning.
class BusyTerminalWarningBrowserTest : public UwTerminalBrowserTest {
 protected:
  void SetUpOnMainThread() override {
    UwTerminalBrowserTest::SetUpOnMainThread();
    CloseWarning::AskInTestsForTesting(true);
  }

  void TearDownOnMainThread() override {
    CloseWarning::AskInTestsForTesting(false);
    UwTerminalBrowserTest::TearDownOnMainThread();
  }

  TabStripModel* tabs() { return browser()->tab_strip_model(); }
  TabOrganizationController* controller() {
    return TabOrganizationController::From(browser());
  }
  bool IsOpen(content::WebContents* page) {
    return tabs()->GetIndexOfWebContents(page) != TabStripModel::kNoTab;
  }
  void CloseTab(content::WebContents* page) {
    tabs()->CloseWebContentsAt(tabs()->GetIndexOfWebContents(page),
                               TabCloseTypes::CLOSE_USER_GESTURE |
                                   TabCloseTypes::CLOSE_CREATE_HISTORICAL_TAB);
  }

  // A terminal tab running `sleep 60`.
  content::WebContents* OpenBusyTerminal() {
    content::WebContents* terminal = OpenTerminal();
    Type(u"sleep 60\r");
    TerminalShell* shell = TerminalShell::From(terminal);
    EXPECT_TRUE(shell);
    bool asking = false;
    bool running = false;
    EXPECT_TRUE(base::test::RunUntil([&] {
      if (!asking) {
        asking = true;
        shell->GetActivity(
            base::BindLambdaForTesting([&](ShellHost::Activity activity) {
              running = activity.program == "sleep";
              asking = false;
            }));
      }
      return running;
    }));
    return terminal;
  }

  views::Widget* Dialog() {
    views::Widget* dialog = nullptr;
    EXPECT_TRUE(base::test::RunUntil([&] {
      dialog = CloseWarning::DialogForTesting(browser());
      return dialog != nullptr;
    }));
    return dialog;
  }
  std::u16string TitleOf(views::Widget* dialog) {
    return dialog->widget_delegate()->GetWindowTitle();
  }
  // Whether the dialog shows a label reading `text`.
  bool Shows(views::Widget* dialog, std::u16string_view text) {
    const std::function<bool(views::View*)> find = [&](views::View* view) {
      auto* label = views::AsViewClass<views::Label>(view);
      if (label && label->GetText() == text) {
        return true;
      }
      return std::ranges::any_of(view->children(), find);
    };
    return find(dialog->GetRootView());
  }
  // Presses the close button or Cancel, and waits until the answer has run.
  void Answer(views::Widget* dialog, bool close) {
    views::DialogDelegate* delegate =
        dialog->widget_delegate()->AsDialogDelegate();
    if (close) {
      delegate->AcceptDialog();
    } else {
      delegate->CancelDialog();
    }
    EXPECT_TRUE(base::test::RunUntil(
        [&] { return !CloseWarning::DialogForTesting(browser()); }));
    base::RunLoop().RunUntilIdle();
  }
};

IN_PROC_BROWSER_TEST_F(BusyTerminalWarningBrowserTest,
                       ClosingABusyTerminalAsksFirst) {
  content::WebContents* busy = OpenBusyTerminal();
  CloseTab(busy);
  views::Widget* dialog = Dialog();
  ASSERT_TRUE(dialog);
  EXPECT_EQ(TitleOf(dialog), u"Close this tab?");
  EXPECT_TRUE(Shows(dialog, u"sleep"));
  SaveWidgetScreenshot(dialog, "busy-terminal-tab");
  Answer(dialog, false);
  EXPECT_TRUE(IsOpen(busy));

  CloseTab(busy);
  Answer(Dialog(), true);
  EXPECT_TRUE(base::test::RunUntil([&] { return !IsOpen(busy); }));
}

IN_PROC_BROWSER_TEST_F(BusyTerminalWarningBrowserTest,
                       AnIdleTerminalClosesWithoutAsking) {
  content::WebContents* idle = OpenTerminal();
  CloseTab(idle);
  EXPECT_TRUE(base::test::RunUntil([&] { return !IsOpen(idle); }));
  EXPECT_FALSE(CloseWarning::DialogForTesting(browser()));
}

// Closing several tabs counts them all and names only the busy one.
IN_PROC_BROWSER_TEST_F(BusyTerminalWarningBrowserTest,
                       ClosingSeveralTabsCountsThem) {
  content::WebContents* page = contents();
  content::WebContents* idle = OpenTerminal();
  content::WebContents* busy = OpenBusyTerminal();
  tabs()->ExecuteCloseTabsCommand(base::BindLambdaForTesting([&] {
                                    return std::vector<tabs::TabInterface*>{
                                        tabs()->GetTabForWebContents(idle),
                                        tabs()->GetTabForWebContents(busy)};
                                  }),
                                  false);
  views::Widget* dialog = Dialog();
  EXPECT_EQ(TitleOf(dialog), u"Close 2 tabs?");
  Answer(dialog, true);
  EXPECT_TRUE(
      base::test::RunUntil([&] { return !IsOpen(idle) && !IsOpen(busy); }));
  EXPECT_TRUE(IsOpen(page));
}

// Closing a task asks, with its rows tinted while it does, and closes the
// task once the user agrees.
IN_PROC_BROWSER_TEST_F(BusyTerminalWarningBrowserTest,
                       ClosingATaskWithABusyTerminalAsksFirst) {
  content::WebContents* page = contents();
  content::WebContents* busy = OpenBusyTerminal();
  const std::optional<tab_groups::TabGroupId> task =
      controller()->MoveToNewTask(
          {*controller()->IdFor(tabs()->GetTabForWebContents(busy))},
          u"Build shop-web");
  ASSERT_TRUE(task);
  tabs()->ActivateTabAt(tabs()->GetIndexOfWebContents(page));
  controller()->CloseTask(*task);
  views::Widget* dialog = Dialog();
  EXPECT_EQ(TitleOf(dialog), u"Close this task?");
  EXPECT_TRUE(IsTaskCloseHinted(browser(), *task));
  SaveWindowScreenshot(browser(), "busy-terminal-task");
  Answer(dialog, true);
  EXPECT_TRUE(base::test::RunUntil(
      [&] { return !tabs()->group_model()->ContainsTabGroup(*task); }));
  EXPECT_FALSE(IsTaskCloseHinted(browser(), *task));
  EXPECT_TRUE(IsOpen(page));
}

// Ctrl+Cmd+W asks before it adds the blank tab a window keeps, so Cancel
// leaves the window as it was.
IN_PROC_BROWSER_TEST_F(BusyTerminalWarningBrowserTest,
                       CtrlCmdWAsksBeforeAddingABlankTab) {
  content::WebContents* page = contents();
  content::WebContents* busy = OpenBusyTerminal();
  const std::optional<tab_groups::TabGroupId> task =
      controller()->MoveToNewTask(
          {*controller()->IdFor(tabs()->GetTabForWebContents(busy))},
          u"Build shop-web");
  ASSERT_TRUE(task);
  CloseTab(page);
  ASSERT_TRUE(base::test::RunUntil([&] { return tabs()->count() == 1; }));

  chrome::ExecuteCommand(browser(), IDC_CLOSE_TAB_GROUP);
  views::Widget* dialog = Dialog();
  EXPECT_EQ(TitleOf(dialog), u"Close this task?");
  Answer(dialog, false);
  EXPECT_EQ(tabs()->count(), 1);
  EXPECT_TRUE(tabs()->group_model()->ContainsTabGroup(*task));
}

IN_PROC_BROWSER_TEST_F(BusyTerminalWarningBrowserTest,
                       ClosingTheWindowAsksFirst) {
  OpenBusyTerminal();
  const int count = tabs()->count();
  chrome::CloseWindow(browser());
  views::Widget* dialog = Dialog();
  EXPECT_EQ(TitleOf(dialog), u"Close this window?");
  EXPECT_TRUE(Shows(dialog, u"Scratchpad"));
  SaveWidgetScreenshot(dialog, "busy-terminal-window");
  Answer(dialog, false);
  EXPECT_EQ(tabs()->count(), count);
}

IN_PROC_BROWSER_TEST_F(BusyTerminalWarningBrowserTest, QuittingAsksFirst) {
  OpenBusyTerminal();
  const int count = tabs()->count();
  chrome::CloseAllBrowsersAndQuit();
  views::Widget* dialog = Dialog();
  EXPECT_EQ(TitleOf(dialog), u"Quit uw?");
  SaveWidgetScreenshot(dialog, "busy-terminal-quit");
  Answer(dialog, false);
  EXPECT_TRUE(
      base::test::RunUntil([] { return !browser_shutdown::IsTryingToQuit(); }));
  EXPECT_EQ(tabs()->count(), count);
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
