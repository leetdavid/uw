// Copyright 2026 The uw Authors. All rights reserved.

#include <cmath>
#include <functional>
#include <memory>
#include <string>

#include "base/functional/bind.h"
#include "base/run_loop.h"
#include "base/test/run_until.h"
#include "chrome/browser/headless/test/headless_mode_browsertest.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/tabs/common/tab_view.h"
#include "chrome/test/base/ui_test_utils.h"
#include "content/public/browser/render_widget_host_view.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "net/test/embedded_test_server/http_request.h"
#include "net/test/embedded_test_server/http_response.h"
#include "ui/base/clipboard/clipboard.h"
#include "ui/base/clipboard/clipboard_buffer.h"
#include "ui/base/clipboard/test/clipboard_test_util.h"
#include "ui/base/clipboard/test/test_clipboard.h"
#include "ui/base/test/ui_controls.h"
#include "ui/gfx/geometry/point.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/views/view_utils.h"
#include "ui/views/widget/widget.h"

namespace uw {
namespace {

constexpr char16_t kWord[] = u"brew install --cask leetdavid/uw/uw";

// A page whose button copies a command, as a site's copy button does. It
// keeps what happened, with whether the page had focus at the click.
constexpr char kCopyPage[] = R"(<!doctype html>
<button id="copy" style="position: fixed; left: 40px; top: 40px;
    width: 240px; height: 80px">Copy</button>
<script>
  window.result = null;
  document.getElementById('copy').addEventListener('click', async () => {
    const focused = document.hasFocus();
    try {
      await navigator.clipboard.writeText(
          'brew install --cask leetdavid/uw/uw');
      window.result = 'copied, focused ' + focused;
    } catch (error) {
      window.result =
          error.name + ': ' + error.message + ', focused ' + focused;
    }
  });
</script>)";

std::unique_ptr<net::test_server::HttpResponse> ServeCopyPage(
    const net::test_server::HttpRequest& request) {
  if (request.relative_url != "/copy") {
    return nullptr;
  }
  auto response = std::make_unique<net::test_server::BasicHttpResponse>();
  response->set_content_type("text/html");
  response->set_content(kCopyPage);
  return response;
}

class PageClipboardBrowserTest : public headless::HeadlessModeBrowserTest {
 protected:
  void SetUpOnMainThread() override {
    headless::HeadlessModeBrowserTest::SetUpOnMainThread();
    ui::TestClipboard::CreateForCurrentThread();
    embedded_test_server()->RegisterRequestHandler(
        base::BindRepeating(&ServeCopyPage));
    ASSERT_TRUE(embedded_test_server()->Start());
  }

  void TearDownOnMainThread() override {
    ui::Clipboard::DestroyClipboardForCurrentThread();
    headless::HeadlessModeBrowserTest::TearDownOnMainThread();
  }

  content::WebContents* OpenCopyPage() {
    EXPECT_TRUE(ui_test_utils::NavigateToURL(
        browser(), embedded_test_server()->GetURL("/copy")));
    return browser()->tab_strip_model()->GetActiveWebContents();
  }

  // What the page's click did, once it has finished.
  static std::string Result(content::WebContents* page) {
    std::string result;
    EXPECT_TRUE(base::test::RunUntil([&] {
      const content::EvalJsResult value = content::EvalJs(
          page, "window.result", content::EXECUTE_SCRIPT_NO_USER_GESTURE);
      if (!value.is_string()) {
        return false;
      }
      result = value.ExtractString();
      return true;
    }));
    return result;
  }

  static std::u16string ClipboardText() {
    return ui::clipboard_test_util::ReadText(
        ui::Clipboard::GetForCurrentThread(), ui::ClipboardBuffer::kCopyPaste,
        /*data_dst=*/nullptr);
  }

  // The middle of the button, on the screen. The page's pixels need not be
  // the screen's points, so they are scaled by the page's width on the
  // screen.
  static gfx::Point ButtonOnScreen(content::WebContents* page) {
    const content::EvalJsResult result = content::EvalJs(
        page,
        "(r => [r.left + r.width / 2, r.top + r.height / 2, innerWidth])("
        "    document.getElementById('copy').getBoundingClientRect())");
    const base::ListValue& at = result.ExtractList();
    const gfx::Rect view = page->GetRenderWidgetHostView()->GetViewBounds();
    const double scale = view.width() / at[2].GetDouble();
    return gfx::Point(view.x() + std::lround(at[0].GetDouble() * scale),
                      view.y() + std::lround(at[1].GetDouble() * scale));
  }

  // Brings the window to the front and waits for the page to stop moving on
  // the screen.
  void Settle(content::WebContents* page) {
    browser()->GetWindow()->Activate();
    gfx::Rect last;
    int still = 0;
    ASSERT_TRUE(base::test::RunUntil([&] {
      const gfx::Rect now = page->GetRenderWidgetHostView()->GetViewBounds();
      still = now == last ? still + 1 : 0;
      last = now;
      return browser()->GetWindow()->IsActive() && still >= 10;
    }));
  }

  // The middle of the active tab's row in the sidebar, on the screen.
  gfx::Point ActiveTabRowOnScreen() {
    tabs::TabInterface* active = browser()->tab_strip_model()->GetActiveTab();
    TabView* found = nullptr;
    const std::function<void(views::View*)> visit = [&](views::View* view) {
      if (!view->IsDrawn()) {
        return;
      }
      if (auto* row = views::AsViewClass<TabView>(view);
          row && row->GetTabInterface() == active) {
        found = row;
      }
      for (views::View* child : view->children()) {
        visit(child);
      }
    };
    visit(BrowserView::GetBrowserViewForBrowser(browser())
              ->GetWidget()
              ->GetRootView());
    CHECK(found);
    return found->GetBoundsInScreen().CenterPoint();
  }

  // Clicks the real mouse. Chromium asks macOS where the pointer is and
  // which button is held, so the events go through ui_controls, which
  // answers for macOS.
  void Click(const gfx::Point& point) {
    ui_controls::EnableUIControls();
    base::RunLoop moved;
    ASSERT_TRUE(ui_controls::SendMouseMoveNotifyWhenDone(
        point.x(), point.y(), moved.QuitClosure(),
        browser()->GetWindow()->GetNativeWindow()));
    moved.Run();
    for (int state : {ui_controls::DOWN, ui_controls::UP}) {
      base::RunLoop done;
      ASSERT_TRUE(ui_controls::SendMouseEventsNotifyWhenDone(
          ui_controls::LEFT, state, done.QuitClosure()));
      done.Run();
    }
  }
};

// A click from a script with the user's gesture copies.
IN_PROC_BROWSER_TEST_F(PageClipboardBrowserTest, ACopyButtonCopies) {
  content::WebContents* page = OpenCopyPage();
  page->Focus();
  ASSERT_TRUE(content::ExecJs(page, "document.getElementById('copy').click()"));
  EXPECT_EQ(Result(page), "copied, focused true");
  EXPECT_EQ(ClipboardText(), kWord);
}

// A real click on a site's copy button in a real window. Run in the
// window-tests VM, as the cases below.
IN_PROC_BROWSER_TEST_F(PageClipboardBrowserTest,
                       MANUAL_ACopyButtonCopiesWithARealClick) {
  content::WebContents* page = OpenCopyPage();
  Settle(page);
  Click(ButtonOnScreen(page));
  EXPECT_EQ(Result(page), "copied, focused true");
  EXPECT_EQ(ClipboardText(), kWord);
}

// After a click on the tab's row in the sidebar, which takes focus there.
IN_PROC_BROWSER_TEST_F(PageClipboardBrowserTest,
                       MANUAL_ACopyButtonCopiesAfterAClickOnItsTab) {
  content::WebContents* page = OpenCopyPage();
  Settle(page);
  Click(ActiveTabRowOnScreen());
  Click(ButtonOnScreen(page));
  EXPECT_EQ(Result(page), "copied, focused true");
  EXPECT_EQ(ClipboardText(), kWord);
}

// After the address bar had focus.
IN_PROC_BROWSER_TEST_F(PageClipboardBrowserTest,
                       MANUAL_ACopyButtonCopiesAfterTheAddressBar) {
  content::WebContents* page = OpenCopyPage();
  Settle(page);
  chrome::FocusLocationBar(browser());
  Click(ButtonOnScreen(page));
  EXPECT_EQ(Result(page), "copied, focused true");
  EXPECT_EQ(ClipboardText(), kWord);
}

}  // namespace
}  // namespace uw
