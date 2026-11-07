// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/browser/ui/views/tab_organization/mouse_actions.h"

#include <windows.h>

#include <functional>
#include <optional>
#include <string_view>
#include <tuple>
#include <vector>

#include "base/run_loop.h"
#include "base/task/sequenced_task_runner.h"
#include "base/test/run_until.h"
#include "base/time/time.h"
#include "chrome/browser/headless/test/headless_mode_browsertest.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/browser/ui/navigator/browser_navigator.h"
#include "chrome/browser/ui/navigator/browser_navigator_params.h"
#include "chrome/browser/ui/tabs/tab_enums.h"
#include "chrome/browser/ui/tabs/tab_group_model.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/tabs/common/tab_view.h"
#include "chrome/browser/ui/views/tabs/tab/tab_close_button.h"
#include "components/prefs/pref_service.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/test/browser_test.h"
#include "ui/aura/window.h"
#include "ui/aura/window_tree_host.h"
#include "ui/base/hit_test.h"
#include "ui/base/win/foreground_helper.h"
#include "ui/display/win/screen_win.h"
#include "ui/events/base_event_utils.h"
#include "ui/events/event.h"
#include "ui/events/event_sink.h"
#include "ui/gfx/scoped_animation_duration_scale_mode.h"
#include "ui/gfx/win/singleton_hwnd.h"
#include "ui/views/view_utils.h"
#include "ui/views/widget/widget.h"
#include "url/gurl.h"
#include "uw/browser/tab_organization/ai_connection.h"
#include "uw/browser/tab_organization/tab_organization_controller.h"
#include "uw/browser/tab_organization/window_screenshot.h"
#include "uw/browser/ui/views/tab_organization/key_hint.h"
#include "uw/browser/ui/views/tab_organization/task_name_field.h"

namespace uw {
namespace {

// The mouse actions and key hints on Windows, where Option is Alt. The Mac's
// are MouseActionsBrowserTest. See docs/plans/keyboard-and-mouse.md.
class MouseActionsWinBrowserTest : public headless::HeadlessModeBrowserTest {
 protected:
  // Browser tests on Windows draw nothing unless asked to, and some save a
  // screenshot.
  MouseActionsWinBrowserTest() { EnablePixelOutput(); }

  TabStripModel* tabs() { return browser()->tab_strip_model(); }
  PrefService* prefs() { return browser()->GetProfile()->GetPrefs(); }
  views::Widget* widget() {
    return BrowserView::GetBrowserViewForBrowser(browser())->GetWidget();
  }

  void Settle() { base::RunLoop().RunUntilIdle(); }

  tabs::TabInterface* OpenLinkFrom(tabs::TabInterface* source) {
    NavigateParams params(browser(), GURL("about:blank"),
                          ui::PAGE_TRANSITION_LINK);
    params.disposition = WindowOpenDisposition::NEW_BACKGROUND_TAB;
    params.source_contents = source->GetContents();
    Navigate(&params);
    Settle();
    return tabs()->GetTabForWebContents(params.navigated_or_inserted_contents);
  }

  // The first view in the window that `match` takes.
  views::View* Find(const std::function<bool(views::View*)>& match) {
    widget()->LayoutRootViewIfNecessary();
    views::View* found = nullptr;
    const std::function<void(views::View*)> visit = [&](views::View* view) {
      if (found) {
        return;
      }
      if (match(view)) {
        found = view;
        return;
      }
      for (views::View* child : view->children()) {
        visit(child);
      }
    };
    visit(widget()->GetRootView());
    return found;
  }

  views::View* RowOf(tabs::TabInterface* tab) {
    return Find([&](views::View* view) {
      auto* row = views::AsViewClass<TabView>(view);
      return row && row->GetTabInterface() == tab && row->IsDrawn();
    });
  }
  views::View* RowOf(const tab_groups::TabGroupId& task) {
    views::View* field = Find([&](views::View* view) {
      auto* field = views::AsViewClass<TaskNameField>(view);
      return field && field->group() == task && field->parent()->IsDrawn();
    });
    return field ? field->parent() : nullptr;
  }
  // The part of class T on the row of `tab`.
  template <typename T>
  T* PartOf(tabs::TabInterface* tab) {
    views::View* row = RowOf(tab);
    for (views::View* child : row ? row->children() : views::View::Views()) {
      if (T* part = views::AsViewClass<T>(child)) {
        return part;
      }
    }
    return nullptr;
  }
  // The empty space below the sidebar's tabs.
  views::View* BelowTheTabs() {
    return Find([](views::View* view) {
      return std::string_view(view->GetClassName()) ==
                 "VerticalTabStripBottomContainer" &&
             view->IsDrawn();
    });
  }

  // Alt goes down or up as Windows hands it to the window.
  void PressAlt(bool down) {
    ui::KeyEvent event(
        down ? ui::EventType::kKeyPressed : ui::EventType::kKeyReleased,
        ui::VKEY_MENU, down ? ui::EF_ALT_DOWN : ui::EF_NONE);
    std::ignore = widget()
                      ->GetNativeWindow()
                      ->GetHost()
                      ->GetEventSink()
                      ->OnEventFromSource(&event);
  }

  HWND Hwnd() {
    return widget()->GetNativeWindow()->GetHost()->GetAcceleratedWidget();
  }
  // `point` on the screen, in Windows' pixels.
  static gfx::Point InPixels(const gfx::Point& point) {
    return display::win::GetScreenWin()->DIPToScreenPoint(point);
  }
  void Wait(base::TimeDelta delay) {
    base::RunLoop run;
    base::SequencedTaskRunner::GetCurrentDefault()->PostDelayedTask(
        FROM_HERE, run.QuitClosure(), delay);
    run.Run();
  }

  // A click on the middle of `view` where it drags the window, as Windows
  // sends one: `message` is WM_NCLBUTTONDOWN, or WM_NCLBUTTONDBLCLK for the
  // second click of a double-click.
  void ClickCaption(views::View* view, UINT message) {
    const gfx::Point point = InPixels(view->GetBoundsInScreen().CenterPoint());
    for (UINT each : {message, static_cast<UINT>(WM_NCLBUTTONUP)}) {
      ::SendMessage(Hwnd(), each, HTCAPTION, MAKELPARAM(point.x(), point.y()));
    }
    Settle();
  }

  // For a real window: the window in front, Windows' own pointer on the
  // middle of `view`, and keys and buttons as the keyboard and mouse send
  // them.
  void BringToFront() {
    ASSERT_TRUE(SUCCEEDED(ui::ForegroundHelper::SetForeground(Hwnd())));
    ASSERT_TRUE(base::test::RunUntil([&] {
      return ::GetForegroundWindow() == Hwnd() &&
             browser()->GetWindow()->IsActive();
    }));
  }
  void PointAt(views::View* view) {
    // Off the view first, so Windows reports a move even when the pointer
    // was already there.
    const gfx::Point away =
        InPixels(widget()->GetWindowBoundsInScreen().CenterPoint());
    ASSERT_TRUE(::SetCursorPos(away.x(), away.y()));
    Wait(base::Milliseconds(200));
    // A new row grows into place, so wait until its middle is its own.
    ASSERT_TRUE(base::test::RunUntil([&] {
      widget()->LayoutRootViewIfNecessary();
      gfx::Point middle = view->GetLocalBounds().CenterPoint();
      views::View::ConvertPointToWidget(view, &middle);
      return view->Contains(
          widget()->GetRootView()->GetEventHandlerForPoint(middle));
    }));
    const gfx::Point point = InPixels(view->GetBoundsInScreen().CenterPoint());
    ASSERT_TRUE(::SetCursorPos(point.x(), point.y()));
    Wait(base::Milliseconds(200));
  }
  static INPUT Alt(bool down) {
    INPUT input = {.type = INPUT_KEYBOARD};
    input.ki.wVk = VK_MENU;
    input.ki.dwFlags = down ? 0 : KEYEVENTF_KEYUP;
    return input;
  }
  static INPUT Button(DWORD flags) {
    INPUT input = {.type = INPUT_MOUSE};
    input.mi.dwFlags = flags;
    return input;
  }
  void Send(std::vector<INPUT> inputs) {
    ASSERT_EQ(::SendInput(inputs.size(), inputs.data(), sizeof(INPUT)),
              inputs.size());
  }

 private:
  // Rows sit still under a real pointer.
  gfx::ScopedAnimationDurationScaleMode instant_{
      gfx::ScopedAnimationDurationScaleMode::ZERO_DURATION};
};

// Alt on a parent's close button tints the tabs an Alt-click would close, and
// the tooltip names Alt. Letting go, or switching to another app, takes the
// tint away.
IN_PROC_BROWSER_TEST_F(MouseActionsWinBrowserTest,
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

// Alt on the rest of a parent's row fades the tabs an Alt-drag would leave
// behind, and the row's tooltip says so.
IN_PROC_BROWSER_TEST_F(MouseActionsWinBrowserTest,
                       AltOnAParentsRowFadesTheTabsThatStay) {
  tabs::TabInterface* parent = tabs()->GetTabAtIndex(0);
  tabs::TabInterface* child = OpenLinkFrom(parent);
  RowOf(parent)->OnMouseEntered(
      ui::MouseEvent(ui::EventType::kMouseEntered, gfx::Point(), gfx::Point(),
                     ui::EventTimeForNow(), ui::EF_NONE, ui::EF_NONE));

  PressAlt(true);
  EXPECT_EQ(PartOf<KeyHint>(parent)->shown(), KeyHint::Shown::kNone);
  EXPECT_EQ(PartOf<KeyHint>(child)->shown(), KeyHint::Shown::kStays);
  EXPECT_EQ(PartOf<KeyHint>(parent)->GetTooltipText(),
            u"Drag to move this tab alone.\nThe tab under it stays.");

  PressAlt(false);
  EXPECT_EQ(PartOf<KeyHint>(child)->shown(), KeyHint::Shown::kNone);
}

// The second click of a double-click below the tabs opens a tab, unless its
// switch is off. Windows sends the window both clicks.
IN_PROC_BROWSER_TEST_F(MouseActionsWinBrowserTest,
                       ADoubleClickBelowTheTabsOpensATab) {
  prefs()->SetBoolean(kCommandBarEnabledPref, false);
  views::View* below = BelowTheTabs();
  ASSERT_TRUE(below);
  const int count = tabs()->count();
  ClickCaption(below, WM_NCLBUTTONDOWN);
  EXPECT_EQ(tabs()->count(), count);

  ClickCaption(below, WM_NCLBUTTONDBLCLK);
  EXPECT_TRUE(
      base::test::RunUntil([&] { return tabs()->count() == count + 1; }));

  prefs()->SetBoolean(kDoubleClickOpensTabPref, false);
  ClickCaption(BelowTheTabs(), WM_NCLBUTTONDBLCLK);
  EXPECT_EQ(tabs()->count(), count + 1);
}

// Run in the Windows VM with --run-manual --headful-mode. With Windows' own
// pointer and keyboard, Alt on a parent's close button tints its branch, and
// an Alt-click closes it.
IN_PROC_BROWSER_TEST_F(MouseActionsWinBrowserTest,
                       MANUAL_AltClickInARealWindow) {
  tabs::TabInterface* parent = tabs()->GetTabAtIndex(0);
  tabs::TabInterface* child = OpenLinkFrom(parent);
  chrome::NewTab(browser(), NewTabTypes::kNewTabCommand);
  Settle();
  tabs::TabInterface* other = tabs()->GetActiveTab();
  tabs()->ActivateTabAt(tabs()->GetIndexOfTab(parent));
  BringToFront();

  views::Button* close = PartOf<TabCloseButton>(parent);
  PointAt(close);
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return close->GetState() == views::Button::STATE_HOVERED; }));

  Send({Alt(true)});
  EXPECT_TRUE(base::test::RunUntil([&] {
    return PartOf<KeyHint>(child)->shown() == KeyHint::Shown::kCloses;
  }));
  SaveWindowScreenshot(browser(), "key-hint-close-windows");
  Send({Button(MOUSEEVENTF_LEFTDOWN), Button(MOUSEEVENTF_LEFTUP), Alt(false)});
  EXPECT_TRUE(base::test::RunUntil([&] { return tabs()->count() == 1; }));
  EXPECT_EQ(tabs()->GetTabAtIndex(0), other);
}

// Run in the Windows VM with --run-manual --headful-mode. A real double-click
// below the tabs opens a tab, and Windows leaves the window's size alone.
IN_PROC_BROWSER_TEST_F(MouseActionsWinBrowserTest,
                       MANUAL_DoubleClickBelowTheTabsInARealWindow) {
  prefs()->SetBoolean(kCommandBarEnabledPref, false);
  BringToFront();
  const int count = tabs()->count();
  PointAt(BelowTheTabs());
  Send({Button(MOUSEEVENTF_LEFTDOWN), Button(MOUSEEVENTF_LEFTUP),
        Button(MOUSEEVENTF_LEFTDOWN), Button(MOUSEEVENTF_LEFTUP)});
  EXPECT_TRUE(
      base::test::RunUntil([&] { return tabs()->count() == count + 1; }));
  EXPECT_FALSE(widget()->IsMaximized());
}

// Run in the Windows VM with --run-manual --headful-mode. A real middle-click
// on a task's row closes the task.
IN_PROC_BROWSER_TEST_F(MouseActionsWinBrowserTest,
                       MANUAL_MiddleClickOnATaskRowInARealWindow) {
  tabs::TabInterface* parent = tabs()->GetTabAtIndex(0);
  OpenLinkFrom(parent);
  chrome::NewTab(browser(), NewTabTypes::kNewTabCommand);
  Settle();
  tabs::TabInterface* other = tabs()->GetActiveTab();
  TabOrganizationController* controller =
      TabOrganizationController::From(browser());
  const std::optional<tab_groups::TabGroupId> task =
      controller->MoveToNewTask({*controller->IdFor(parent)}, u"Plan");
  ASSERT_TRUE(task);
  Settle();
  BringToFront();

  PointAt(RowOf(*task));
  Send({Button(MOUSEEVENTF_MIDDLEDOWN), Button(MOUSEEVENTF_MIDDLEUP)});
  EXPECT_TRUE(base::test::RunUntil([&] { return tabs()->count() == 1; }))
      << tabs()->count();
  EXPECT_FALSE(tabs()->group_model()->ContainsTabGroup(*task));
  EXPECT_EQ(tabs()->GetTabAtIndex(0), other);
}

}  // namespace
}  // namespace uw
