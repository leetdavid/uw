// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/browser/ui/glass/glass.h"

#import <AppKit/AppKit.h>

#include <string>
#include <utility>

#include "base/run_loop.h"
#include "base/strings/string_number_conversions.h"
#include "base/test/run_until.h"
#include "chrome/browser/autocomplete/chrome_autocomplete_scheme_classifier.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/headless/test/headless_mode_browsertest.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/browser/ui/omnibox/omnibox_controller.h"
#include "chrome/browser/ui/omnibox/omnibox_edit_model.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/location_bar/location_bar_view.h"
#include "chrome/browser/ui/views/omnibox/omnibox_popup_presenter_base.h"
#include "chrome/browser/ui/views/omnibox/omnibox_popup_view_webui.h"
#include "chrome/browser/ui/views/omnibox/rounded_omnibox_results_frame.h"
#include "chrome/browser/ui/views/toolbar/toolbar_view.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/omnibox/browser/autocomplete_controller.h"
#include "components/omnibox/browser/autocomplete_input.h"
#include "components/prefs/pref_service.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "third_party/metrics_proto/omnibox_event.pb.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/base/mojom/menu_source_type.mojom.h"
#include "ui/base/test/ui_controls.h"
#include "ui/compositor/layer.h"
#include "ui/menus/simple_menu_model.h"
#include "ui/views/controls/menu/menu_controller.h"
#include "ui/views/controls/menu/menu_item_view.h"
#include "ui/views/controls/menu/menu_runner.h"
#include "ui/views/controls/menu/submenu_view.h"
#include "ui/views/view_utils.h"
#include "ui/views/widget/widget.h"
#include "ui/views/window/non_client_view.h"
#include "uw/browser/tab_organization/window_screenshot.h"
#include "uw/browser/ui/glass/glass_prefs.h"
#include "uw/browser/ui/glass/glass_screenshot.h"
#include "uw/browser/ui/views/tab_organization/new_button.h"
#include "uw/browser/ui/views/tab_organization/organize_bar.h"
#include "uw/browser/workspaces/workspace_theme.h"
#include "uw/components/look/theme.h"

namespace uw {
namespace {

class GlassBrowserTest : public headless::HeadlessModeBrowserTest {
 protected:
  void SetLook(int transparency, int glass) {
    Theme theme = ThemeOfWindow(browser());
    theme.transparency = transparency;
    theme.glass = glass;
    SetThemeOfWindow(browser(), theme);
  }

  static bool MacCanShowGlass() {
    return CanShowGlass() && !ReducesTransparency();
  }

  // The first item of the menu open now.
  static views::MenuItemView* OpenMenu() {
    views::MenuController* controller =
        views::MenuController::GetActiveInstance();
    return controller && controller->GetSelectedMenuItem()
               ? controller->GetSelectedMenuItem()->GetRootMenuItem()
               : nullptr;
  }

  // The bar at the foot of the sidebar under `view`.
  static OrganizeBar* FindBar(views::View* view) {
    if (OrganizeBar* bar = views::AsViewClass<OrganizeBar>(view)) {
      return bar;
    }
    for (views::View* child : view->children()) {
      if (OrganizeBar* bar = FindBar(child)) {
        return bar;
      }
    }
    return nullptr;
  }

  // macOS's glass behind everything `widget` draws, or nil.
  static NSView* GlassBehind(views::Widget* widget) {
    NSView* back = widget->GetNativeWindow()
                       .GetNativeNSWindow()
                       .contentView.subviews.firstObject;
    return [back isKindOfClass:NSClassFromString(@"NSGlassEffectView")] ? back
                                                                        : nil;
  }

  // The address bar's suggestion list.
  OmniboxPopupPresenterBase* Suggestions() {
    return static_cast<OmniboxPopupViewWebUI*>(
               BrowserView::GetBrowserViewForBrowser(browser())
                   ->toolbar()
                   ->location_bar_view()
                   ->GetOmniboxPopupView())
        ->presenter();
  }
};

IN_PROC_BROWSER_TEST_F(GlassBrowserTest, ASolidThemeAsksForNoGlass) {
  EXPECT_EQ(ThemeOfWindow(browser()).transparency, 0);
  EXPECT_EQ(ThemeOfWindow(browser()).glass, 0);
  EXPECT_FALSE(WantsGlass(browser()));
  EXPECT_FALSE(ShowsGlass(browser()));
  EXPECT_EQ(CoverOfWindow(browser()), 1.0f);
  EXPECT_EQ(ToolbarCoverOfWindow(browser(), SK_ColorWHITE), 1.0f);
  EXPECT_EQ(ElementGlassOfWindow(browser()), 0);
}

IN_PROC_BROWSER_TEST_F(GlassBrowserTest, GlassAloneLeavesTheWindowSolid) {
  SetLook(0, 60);
  EXPECT_FALSE(WantsGlass(browser()));
  EXPECT_EQ(ElementGlassOfWindow(browser()), MacCanShowGlass() ? 60 : 0);

  SetLook(50, 60);
  EXPECT_EQ(WantsGlass(browser()), MacCanShowGlass());
}

IN_PROC_BROWSER_TEST_F(GlassBrowserTest,
                       WindowsBehindKeepGlassUntilTheSettingIsOff) {
  EXPECT_GT(MaxGlassWindows(), 1u);
  g_browser_process->local_state()->SetBoolean(kGlassBehindPref, false);
  EXPECT_EQ(MaxGlassWindows(), 1u);
}

IN_PROC_BROWSER_TEST_F(GlassBrowserTest, TransparencyLetsTheGlassThrough) {
  if (!MacCanShowGlass()) {
    GTEST_SKIP() << "This Mac draws no glass.";
  }
  browser()->GetWindow()->Activate();
  SetLook(kFullTransparency / 2, 0);
  ASSERT_TRUE(base::test::RunUntil([&] { return ShowsGlass(browser()); }));
  EXPECT_FLOAT_EQ(CoverOfWindow(browser()), 0.6f);
  EXPECT_FLOAT_EQ(ToolbarCoverOfWindow(browser(), SK_ColorWHITE), 0.6f);
  SaveWindowScreenshot(browser(), "transparency-half");

  SetLook(kFullTransparency, kFullGlass);
  EXPECT_FLOAT_EQ(CoverOfWindow(browser()), 0.2f);
  EXPECT_NEAR(ToolbarCoverOfWindow(browser(), SK_ColorWHITE), 0.52f, 0.011f);
  EXPECT_EQ(ElementGlassOfWindow(browser()), kFullGlass);

  SetLook(0, 0);
  ASSERT_TRUE(base::test::RunUntil([&] { return !ShowsGlass(browser()); }));
}

// The frame hears of the glass with the theme, so it never paints the old look
// under a toolbar that paints the new one.
IN_PROC_BROWSER_TEST_F(GlassBrowserTest, TheFrameTurnsToGlassWithTheTheme) {
  if (!MacCanShowGlass()) {
    GTEST_SKIP() << "This Mac draws no glass.";
  }
  browser()->GetWindow()->Activate();
  const ui::Layer* frame = BrowserView::GetBrowserViewForBrowser(browser())
                               ->GetWidget()
                               ->non_client_view()
                               ->frame_view()
                               ->layer();
  SetLook(kFullTransparency / 2, 0);
  ASSERT_TRUE(ShowsGlass(browser()));
  EXPECT_FALSE(frame->fills_bounds_opaquely());

  SetLook(0, 0);
  ASSERT_FALSE(ShowsGlass(browser()));
  EXPECT_TRUE(frame->fills_bounds_opaquely());
}

// A menu uw frosts lays Chromium's menu color over frosted glass while the
// window's tiles and buttons are glass, and stays solid while they are flat.
IN_PROC_BROWSER_TEST_F(GlassBrowserTest,
                       AMenuIsFrostedWhileTheElementsAreGlass) {
  if (!MacCanShowGlass()) {
    GTEST_SKIP() << "This Mac draws no glass.";
  }
  ui::SimpleMenuModel model(nullptr);
  model.AddItem(1, u"New tab");
  for (const int glass : {0, 60}) {
    SetLook(0, glass);
    views::MenuRunner menu(&model, views::MenuRunner::HAS_MNEMONICS);
    menu.RunMenuAt(
        BrowserView::GetBrowserViewForBrowser(browser())->GetWidget(), nullptr,
        gfx::Rect(100, 100, 0, 0), views::MenuAnchorPosition::kTopLeft,
        ui::mojom::MenuSourceType::kNone);
    FrostOpenMenu(browser());
    ASSERT_TRUE(OpenMenu());
    EXPECT_EQ(!!GlassBehind(OpenMenu()->GetSubmenu()->GetWidget()), glass > 0);
    menu.Cancel();
    ASSERT_TRUE(base::test::RunUntil([] { return !OpenMenu(); }));
  }
}

// The address bar's suggestion list frosts like a menu, leaving the address it
// opens around sharp, and is solid again once the elements are flat.
IN_PROC_BROWSER_TEST_F(GlassBrowserTest,
                       TheSuggestionListIsFrostedWhileTheElementsAreGlass) {
  if (!MacCanShowGlass()) {
    GTEST_SKIP() << "This Mac draws no glass.";
  }
  OmniboxPopupPresenterBase* suggestions = Suggestions();
  for (const int glass : {60, 0}) {
    SetLook(0, glass);
    suggestions->Show();
    views::Widget* list = suggestions->get_widget_for_testing();
    ASSERT_TRUE(list);
    NSView* back = GlassBehind(list);
    EXPECT_EQ(!!back, glass > 0);
    EXPECT_EQ(!!back.layer.mask, glass > 0);
    // The list grows as suggestions come in, and the glass with it.
    const CGFloat glass_height = NSHeight(back.frame);
    gfx::Rect taller = list->GetWindowBoundsInScreen();
    taller.set_height(taller.height() + 100);
    list->SetBounds(taller);
    EXPECT_EQ(NSHeight(back.frame) - glass_height, back ? 100 : 0);
    suggestions->Hide();
  }
}

// Real windows only: macOS draws glass behind a window on screen. Saves how
// macOS shows the window, so the look can be checked by eye.
IN_PROC_BROWSER_TEST_F(GlassBrowserTest, MANUAL_GlassAsMacOSShowsIt) {
  if (!MacCanShowGlass()) {
    GTEST_SKIP() << "This Mac draws no glass.";
  }
  browser()->GetWindow()->Activate();
  for (const auto& [transparency, glass] :
       {std::pair{0, kFullGlass}, std::pair{kFullTransparency / 2, 40},
        std::pair{kFullTransparency, kFullGlass}}) {
    SetLook(transparency, glass);
    ASSERT_TRUE(base::test::RunUntil(
        [&] { return ShowsGlass(browser()) == (transparency > 0); }));
    CaptureWindow(browser());
    SaveNativeWindowScreenshot(
        browser(), "glass-native-" + base::NumberToString(transparency) + "-" +
                       base::NumberToString(glass));
  }

  Theme dark = ThemeOfWindow(browser());
  dark.transparency = kFullTransparency / 2;
  dark.glass = kFullGlass / 2;
  dark.mode = ThemeMode::kDark;
  SetThemeOfWindow(browser(), dark);
  CaptureWindow(browser());
  SaveNativeWindowScreenshot(browser(), "glass-native-dark");

  // A second window in front: the first keeps its glass, frosted.
  Browser* front = CreateBrowser(browser()->GetProfile());
  front->GetWindow()->Activate();
  ASSERT_TRUE(base::test::RunUntil([&] { return ShowsGlass(front); }));
  EXPECT_TRUE(ShowsGlass(browser()));
  CaptureWindow(front);
  SaveNativeWindowScreenshot(browser(), "glass-native-behind");
}

// Real windows only. Saves the ⋮ menu and a submenu that the pointer opens as
// macOS shows them over glass, so the frosting can be checked by eye.
IN_PROC_BROWSER_TEST_F(GlassBrowserTest, MANUAL_MenusAsMacOSShowsThem) {
  if (!MacCanShowGlass()) {
    GTEST_SKIP() << "This Mac draws no glass.";
  }
  browser()->GetWindow()->Activate();
  SetLook(kFullTransparency / 2, 60);
  chrome::ShowAppMenu(browser());
  ASSERT_TRUE(base::test::RunUntil([] {
    return OpenMenu() && OpenMenu()->GetSubmenu()->GetWidget() &&
           OpenMenu()->GetSubmenu()->GetWidget()->IsVisible();
  }));
  views::MenuItemView* menu = OpenMenu();
  views::Widget* menu_window = menu->GetSubmenu()->GetWidget();
  EXPECT_TRUE(GlassBehind(menu_window));
  SaveWindowWithPopups(browser(), "glass-menu");

  views::MenuItemView* parent = nullptr;
  for (views::MenuItemView* item : menu->GetSubmenu()->GetMenuItems()) {
    if (item->HasSubmenu()) {
      parent = item;
      break;
    }
  }
  ASSERT_TRUE(parent);
  ui_controls::EnableUIControls();
  const gfx::Point center = parent->GetBoundsInScreen().CenterPoint();
  base::RunLoop moved;
  ASSERT_TRUE(ui_controls::SendMouseMoveNotifyWhenDone(
      center.x(), center.y(), moved.QuitClosure(),
      menu_window->GetNativeWindow()));
  moved.Run();
  ASSERT_TRUE(base::test::RunUntil([&] { return parent->SubmenuIsShowing(); }));
  views::Widget* submenu_window = parent->GetSubmenu()->GetWidget();
  EXPECT_TRUE(GlassBehind(submenu_window));
  SaveWindowWithPopups(browser(), "glass-submenu");

  // The new button's menu opens over the sidebar.
  views::MenuController::GetActiveInstance()->Cancel(
      views::MenuController::ExitType::kAll);
  ASSERT_TRUE(base::test::RunUntil([] { return !OpenMenu(); }));
  NewButton* button =
      FindBar(BrowserView::GetBrowserViewForBrowser(browser()))->new_button();
  button->plus()->ShowContextMenu(
      button->plus()->GetBoundsInScreen().CenterPoint(),
      ui::mojom::MenuSourceType::kMouse);
  ASSERT_TRUE(base::test::RunUntil([] {
    return OpenMenu() && OpenMenu()->GetSubmenu()->GetWidget() &&
           OpenMenu()->GetSubmenu()->GetWidget()->IsVisible();
  }));
  EXPECT_TRUE(GlassBehind(OpenMenu()->GetSubmenu()->GetWidget()));
  SaveWindowWithPopups(browser(), "glass-new-button-menu");
}

// Real windows only. Saves the address bar's suggestion list over a red page
// as macOS shows it, frosted around the sharp address.
IN_PROC_BROWSER_TEST_F(GlassBrowserTest, MANUAL_SuggestionsAsMacOSShowsThem) {
  if (!MacCanShowGlass()) {
    GTEST_SKIP() << "This Mac draws no glass.";
  }
  browser()->GetWindow()->Activate();
  ASSERT_TRUE(
      content::ExecJs(browser()->tab_strip_model()->GetActiveWebContents(),
                      "document.body.style.background = '#c62828'"));
  SetLook(kFullTransparency / 2, 60);
  LocationBarView* address = BrowserView::GetBrowserViewForBrowser(browser())
                                 ->toolbar()
                                 ->location_bar_view();
  OmniboxController* omnibox = address->GetOmniboxController();
  omnibox->edit_model()->SetUserText(u"uw glass");
  AutocompleteInput input(
      u"uw glass", metrics::OmniboxEventProto::BLANK,
      ChromeAutocompleteSchemeClassifier(browser()->GetProfile()));
  input.set_omit_asynchronous_matches(true);
  omnibox->autocomplete_controller()->Start(input);
  views::Widget* list = nullptr;
  ASSERT_TRUE(base::test::RunUntil([&] {
    list = Suggestions()->get_widget_for_testing();
    return list && list->IsVisible() &&
           list->GetWindowBoundsInScreen().height() >
               address->GetBoundsInScreen().height() * 3;
  }));
  // The list opens around the address, as Chromium's own test expects.
  EXPECT_EQ(
      list->GetWindowBoundsInScreen().y(),
      address->GetBoundsInScreen().y() -
          RoundedOmniboxResultsFrame::GetLocationBarAlignmentInsets().top() -
          RoundedOmniboxResultsFrame::GetShadowInsets().top());
  EXPECT_TRUE(GlassBehind(list));
  SaveNativeWindowScreenshot(browser(), "glass-suggestions-window");
  SaveWindowWithPopups(browser(), "glass-suggestions");
}

}  // namespace
}  // namespace uw
