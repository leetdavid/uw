// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/browser/ui/views/theme_picker/theme_picker.h"

#import <AppKit/AppKit.h>

#include <vector>

#include "base/run_loop.h"
#include "base/task/single_thread_task_runner.h"
#include "base/test/run_until.h"
#include "base/time/time.h"
#include "chrome/browser/headless/test/headless_mode_browsertest.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/themes/theme_service.h"
#include "chrome/browser/themes/theme_service_factory.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/vertical_tab_strip_region_view.h"
#include "chrome/grit/generated_resources.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "testing/gtest_mac.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/base/l10n/l10n_util_mac.h"
#include "ui/color/color_id.h"
#include "ui/color/color_provider.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/views/widget/widget.h"
#include "url/gurl.h"
#include "uw/browser/tab_organization/commands.h"
#include "uw/browser/tab_organization/window_screenshot.h"
#include "uw/browser/ui/webui/uw_theme_picker/uw_theme_picker_ui.h"
#include "uw/components/look/theme.h"
#include "uw/components/look/theme_prefs.h"
#include "uw/components/look/tint.h"

namespace uw {
namespace {

constexpr SkColor kPeach = SkColorSetRGB(0xF9, 0xE3, 0xD2);
constexpr SkColor kButter = SkColorSetRGB(0xF7, 0xE0, 0x8A);

class ThemePickerBrowserTest : public headless::HeadlessModeBrowserTest {
 protected:
  ThemeService* themes() {
    return ThemeServiceFactory::GetForProfile(browser()->GetProfile());
  }

  ThemePicker* picker() { return ThemePicker::From(browser()); }

  Theme Saved() {
    return ReadTheme(*browser()->GetProfile()->GetPrefs(),
                     themes()->GetUserColor());
  }

  SkColor FrameColor() {
    return BrowserView::GetBrowserViewForBrowser(browser())
        ->GetColorProvider()
        ->GetColor(ui::kColorFrameActive);
  }

  // Opens the picker as View > Theme… does, and waits for its page.
  content::WebContents* Open() {
    chrome::ExecuteCommand(browser(), kShowThemePicker);
    EXPECT_TRUE(base::test::RunUntil([&] { return picker()->IsShowing(); }));
    return picker()->page();
  }
};

IN_PROC_BROWSER_TEST_F(ThemePickerBrowserTest,
                       TheViewMenuAndTheSidebarOpenThePicker) {
  NSMenuItem* item = nil;
  for (NSMenuItem* menu in NSApp.mainMenu.itemArray) {
    item = item ?: [menu.submenu itemWithTag:kShowThemePicker];
  }
  ASSERT_TRUE(item);
  EXPECT_NSEQ(item.title, l10n_util::GetNSString(IDS_UW_THEME_MENU));
  EXPECT_TRUE(BrowserView::GetBrowserViewForBrowser(browser())
                  ->vertical_tab_strip_region_view_for_testing()
                  ->context_menu_controller());

  Open();
}

// A headless bubble never draws its page, so the page is shown in a tab to
// check it against the design.
IN_PROC_BROWSER_TEST_F(ThemePickerBrowserTest, ThePageShowsEveryControl) {
  themes()->SetBrowserColorScheme(ThemeService::BrowserColorScheme::kLight);
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), GURL(kThemePickerUrl)));
  content::WebContents* page =
      browser()->tab_strip_model()->GetActiveWebContents();
  EXPECT_TRUE(base::test::RunUntil([&] {
    return content::EvalJs(page,
                           "document.querySelectorAll('#suggested button')"
                           ".length")
               .ExtractInt() == 10;
  }));
  EXPECT_EQ(1,
            content::EvalJs(page, "document.querySelectorAll('.dot').length"));
  EXPECT_EQ(
      "true",
      content::EvalJs(page, "document.querySelectorAll('#suggested button')[0]"
                            ".getAttribute('aria-pressed')"));
  SaveWindowScreenshot(browser(), "theme-picker");

  themes()->SetBrowserColorScheme(ThemeService::BrowserColorScheme::kDark);
  EXPECT_TRUE(base::test::RunUntil([&] {
    return content::EvalJs(page, "matchMedia('(prefers-color-scheme: dark)')"
                                 ".matches")
        .ExtractBool();
  }));
  SaveWindowScreenshot(browser(), "theme-picker-dark");
}

IN_PROC_BROWSER_TEST_F(ThemePickerBrowserTest, ASuggestedThemeBecomesTheTheme) {
  themes()->SetBrowserColorScheme(ThemeService::BrowserColorScheme::kLight);
  content::WebContents* page = Open();
  ASSERT_TRUE(content::ExecJs(
      page, "document.querySelectorAll('#suggested button')[8].click();"));
  EXPECT_TRUE(base::test::RunUntil(
      [&] { return Saved().colors == std::vector<SkColor>{kPeach, kButter}; }));
  EXPECT_EQ(FrameColor(), TintFrom(kPeach).start);
  EXPECT_EQ("true",
            content::EvalJs(page, "document.querySelectorAll('#suggested "
                                  "button')[8].getAttribute('aria-pressed')"));
}

IN_PROC_BROWSER_TEST_F(ThemePickerBrowserTest, TheFieldAddsUpToThreeDots) {
  content::WebContents* page = Open();
  ASSERT_TRUE(content::ExecJs(page, "document.querySelector('#add').click();"));
  EXPECT_TRUE(
      base::test::RunUntil([&] { return Saved().colors.size() == 2u; }));

  const SkColor before = Saved().colors[1];
  ASSERT_TRUE(content::ExecJs(
      page, "document.querySelectorAll('.dot')[1].dispatchEvent("
            "    new KeyboardEvent('keydown', {key: 'ArrowRight'}));"));
  EXPECT_TRUE(base::test::RunUntil([&] {
    const Theme saved = Saved();
    return saved.colors.size() == 2u && saved.colors[1] != before;
  }));

  ASSERT_TRUE(content::ExecJs(page, "document.querySelector('#add').click();"));
  EXPECT_TRUE(
      base::test::RunUntil([&] { return Saved().colors.size() == 3u; }));
  EXPECT_EQ(true,
            content::EvalJs(page, "document.querySelector('#add').hidden"));
}

IN_PROC_BROWSER_TEST_F(ThemePickerBrowserTest, SlidersAndTheSwitchSaveAtOnce) {
  content::WebContents* page = Open();
  ASSERT_TRUE(content::ExecJs(page, R"(
    const set = (id, value) => {
      const slider = document.getElementById(id);
      slider.value = value;
      slider.dispatchEvent(new Event('input'));
    };
    set('intensity', 40);
    set('grain', 60);
    document.getElementById('colors-in-dark-mode').click();
  )"));
  EXPECT_TRUE(base::test::RunUntil([&] {
    const Theme saved = Saved();
    return saved.intensity == 40 && saved.grain == 60 &&
           saved.colors_in_dark_mode;
  }));
}

// Chromium can keep a closed picker's page, which never asks to be shown again.
IN_PROC_BROWSER_TEST_F(ThemePickerBrowserTest,
                       ThePickerOpensAgainWithTheThemeAsItIsNow) {
  Open();
  picker()->GetWidgetForTesting()->CloseNow();
  ASSERT_FALSE(picker()->IsShowing());

  Theme grainy = Saved();
  grainy.grain = 60;
  WriteTheme(*browser()->GetProfile()->GetPrefs(), grainy);
  content::WebContents* page = Open();
  EXPECT_EQ("60",
            content::EvalJs(page, "document.getElementById('grain').value"));
}

IN_PROC_BROWSER_TEST_F(ThemePickerBrowserTest, TheModeButtonsSetTheMode) {
  content::WebContents* page = Open();
  ASSERT_TRUE(
      content::ExecJs(page, "document.getElementById('dark').click();"));
  EXPECT_TRUE(base::test::RunUntil([&] {
    return themes()->GetBrowserColorScheme() ==
           ThemeService::BrowserColorScheme::kDark;
  }));
  EXPECT_EQ(FrameColor(), SK_ColorBLACK);

  ASSERT_TRUE(
      content::ExecJs(page, "document.getElementById('auto').click();"));
  EXPECT_TRUE(base::test::RunUntil([&] {
    return themes()->GetBrowserColorScheme() ==
           ThemeService::BrowserColorScheme::kSystem;
  }));
}

// A real window can close a bubble as soon as it shows, which a headless one
// never does. Run these with `pnpm run window-tests`.
IN_PROC_BROWSER_TEST_F(ThemePickerBrowserTest,
                       MANUAL_ThePickerOpensAndStaysOpenInARealWindow) {
  chrome::ExecuteCommand(browser(), kShowThemePicker);
  ASSERT_TRUE(picker()->page());
  content::WebContentsConsoleObserver console(picker()->page());
  EXPECT_TRUE(base::test::RunUntil([&] { return picker()->IsShowing(); }))
      << "console: " << console.GetMessageAt(0u);
  base::RunLoop wait;
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE, wait.QuitClosure(), base::Seconds(1));
  wait.Run();
  EXPECT_TRUE(picker()->IsShowing());
  if (views::Widget* bubble = picker()->GetWidgetForTesting()) {
    const gfx::Rect bounds = bubble->GetWindowBoundsInScreen();
    EXPECT_GE(bounds.width(), 300) << bounds.ToString();
    EXPECT_GE(bounds.height(), 380) << bounds.ToString();
    SaveWidgetScreenshot(bubble, "theme-picker-window");
  }
}

IN_PROC_BROWSER_TEST_F(ThemePickerBrowserTest,
                       MANUAL_TheSettingsRowOpensThePickerInARealWindow) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), GURL("chrome://settings/uwOrganization")));
  content::WebContents* settings =
      browser()->tab_strip_model()->GetActiveWebContents();
  ASSERT_TRUE(content::ExecJs(settings, R"(
    (async () => {
      const find = (root) => {
        const found = root.querySelector('settings-uw-organization-page');
        if (found) return found;
        for (const element of root.querySelectorAll('*')) {
          const inner = element.shadowRoot && find(element.shadowRoot);
          if (inner) return inner;
        }
        return null;
      };
      let row = null;
      while (!row) {
        row = find(document)?.shadowRoot?.getElementById('themeLink');
        await new Promise(resolve => setTimeout(resolve, 20));
      }
      row.click();
    })()
  )"));
  EXPECT_TRUE(base::test::RunUntil([&] { return picker()->IsShowing(); }));
}

}  // namespace
}  // namespace uw
