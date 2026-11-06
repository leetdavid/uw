// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/browser/ui/views/tab_organization/key_hint.h"

#include <windows.h>

#include <functional>
#include <tuple>

#include "base/run_loop.h"
#include "base/test/run_until.h"
#include "chrome/browser/headless/test/headless_mode_browsertest.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/browser/ui/navigator/browser_navigator.h"
#include "chrome/browser/ui/navigator/browser_navigator_params.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/tabs/common/tab_view.h"
#include "chrome/browser/ui/views/tabs/tab/tab_close_button.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/test/browser_test.h"
#include "ui/aura/window.h"
#include "ui/aura/window_tree_host.h"
#include "ui/display/win/screen_win.h"
#include "ui/events/event.h"
#include "ui/events/event_sink.h"
#include "ui/gfx/win/singleton_hwnd.h"
#include "ui/views/view_utils.h"
#include "ui/views/widget/widget.h"
#include "url/gurl.h"
#include "uw/browser/tab_organization/window_screenshot.h"

namespace uw {
namespace {

class KeyHintWinBrowserTest : public headless::HeadlessModeBrowserTest {
 protected:
  // Browser tests on Windows draw nothing unless asked to, and one saves a
  // screenshot.
  KeyHintWinBrowserTest() { EnablePixelOutput(); }

  TabStripModel* tabs() { return browser()->tab_strip_model(); }

  tabs::TabInterface* OpenLinkFrom(tabs::TabInterface* source) {
    NavigateParams params(browser(), GURL("about:blank"),
                          ui::PAGE_TRANSITION_LINK);
    params.disposition = WindowOpenDisposition::NEW_BACKGROUND_TAB;
    params.source_contents = source->GetContents();
    Navigate(&params);
    base::RunLoop().RunUntilIdle();
    return tabs()->GetTabForWebContents(params.navigated_or_inserted_contents);
  }

  // The part of class T on the row of `tab`.
  template <typename T>
  T* PartOf(tabs::TabInterface* tab) {
    views::Widget* widget =
        BrowserView::GetBrowserViewForBrowser(browser())->GetWidget();
    widget->LayoutRootViewIfNecessary();
    T* found = nullptr;
    const std::function<void(views::View*)> visit = [&](views::View* view) {
      if (!view->IsDrawn() || found) {
        return;
      }
      if (auto* row = views::AsViewClass<TabView>(view);
          row && row->GetTabInterface() == tab) {
        for (views::View* child : row->children()) {
          if (T* part = views::AsViewClass<T>(child)) {
            found = part;
          }
        }
        return;
      }
      for (views::View* child : view->children()) {
        visit(child);
      }
    };
    visit(widget->GetRootView());
    return found;
  }

  // Alt goes down or up as Windows hands it to the window.
  void PressAlt(bool down) {
    ui::KeyEvent event(
        down ? ui::EventType::kKeyPressed : ui::EventType::kKeyReleased,
        ui::VKEY_MENU, down ? ui::EF_ALT_DOWN : ui::EF_NONE);
    aura::Window* window = BrowserView::GetBrowserViewForBrowser(browser())
                               ->GetWidget()
                               ->GetNativeWindow();
    std::ignore = window->GetHost()->GetEventSink()->OnEventFromSource(&event);
  }

  // Alt goes down or up on the keyboard, for a real window.
  void PressAltOnTheKeyboard(bool down) {
    INPUT input = {.type = INPUT_KEYBOARD};
    input.ki.wVk = VK_MENU;
    input.ki.dwFlags = down ? 0 : KEYEVENTF_KEYUP;
    ASSERT_EQ(::SendInput(1, &input, sizeof(input)), 1u);
  }
};

// Alt on a parent's close button tints the tabs an Alt-click would close, as
// Option does on the Mac, and the tooltip names Alt. Letting go, or switching
// to another app, takes the tint away. See
// docs/plans/keyboard-and-mouse.md#key-hints.
IN_PROC_BROWSER_TEST_F(KeyHintWinBrowserTest,
                       AltOnAParentsCloseButtonTintsItsBranch) {
  tabs::TabInterface* parent = tabs()->GetTabAtIndex(0);
  tabs::TabInterface* child = OpenLinkFrom(parent);
  views::Button* close = PartOf<TabCloseButton>(parent);
  ASSERT_TRUE(close);
  close->SetState(views::Button::STATE_HOVERED);
  EXPECT_EQ(close->GetTooltipText(),
            u"Close tab\nAlt-click to close the tab under it too");
  EXPECT_EQ(PartOf<KeyHint>(child)->shown(), KeyHint::Shown::kNone);

  PressAlt(true);
  EXPECT_EQ(PartOf<KeyHint>(parent)->shown(), KeyHint::Shown::kCloses);
  EXPECT_EQ(PartOf<KeyHint>(child)->shown(), KeyHint::Shown::kCloses);
  EXPECT_EQ(close->GetTooltipText(), u"Close this tab and the tab under it");

  PressAlt(false);
  EXPECT_EQ(PartOf<KeyHint>(child)->shown(), KeyHint::Shown::kNone);

  PressAlt(true);
  EXPECT_EQ(PartOf<KeyHint>(child)->shown(), KeyHint::Shown::kCloses);
  ::SendMessage(gfx::SingletonHwnd::GetInstance()->hwnd(), WM_ACTIVATEAPP,
                FALSE, 0);
  EXPECT_EQ(PartOf<KeyHint>(child)->shown(), KeyHint::Shown::kNone);
}

// Run in the Windows VM with --run-manual --headful-mode. The same with
// Windows' own pointer and keyboard.
IN_PROC_BROWSER_TEST_F(KeyHintWinBrowserTest, MANUAL_AltInARealWindow) {
  tabs::TabInterface* parent = tabs()->GetTabAtIndex(0);
  tabs::TabInterface* child = OpenLinkFrom(parent);
  browser()->GetWindow()->Activate();
  ASSERT_TRUE(
      base::test::RunUntil([&] { return browser()->GetWindow()->IsActive(); }));

  views::Button* close = PartOf<TabCloseButton>(parent);
  const gfx::Point pointer = display::win::GetScreenWin()->DIPToScreenPoint(
      close->GetBoundsInScreen().CenterPoint());
  ASSERT_TRUE(::SetCursorPos(pointer.x(), pointer.y()));
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return close->GetState() == views::Button::STATE_HOVERED; }));

  PressAltOnTheKeyboard(true);
  EXPECT_TRUE(base::test::RunUntil([&] {
    return PartOf<KeyHint>(child)->shown() == KeyHint::Shown::kCloses;
  }));
  SaveWindowScreenshot(browser(), "key-hint-close-windows");

  PressAltOnTheKeyboard(false);
  EXPECT_TRUE(base::test::RunUntil([&] {
    return PartOf<KeyHint>(child)->shown() == KeyHint::Shown::kNone;
  }));
}

}  // namespace
}  // namespace uw
