// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#import <AppKit/AppKit.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <string>
#include <vector>

#include "base/files/file_path.h"
#include "base/run_loop.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/sys_string_conversions.h"
#include "base/task/single_thread_task_runner.h"
#include "base/test/run_until.h"
#include "base/time/time.h"
#include "chrome/app/chrome_command_ids.h"
#include "chrome/browser/autocomplete/tab_matcher_desktop.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/extensions/chrome_test_extension_loader.h"
#include "chrome/browser/extensions/extension_view_host.h"
#include "chrome/browser/global_keyboard_shortcuts_mac.h"
#include "chrome/browser/headless/test/headless_mode_browsertest.h"
#include "chrome/browser/prefs/session_startup_pref.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/profiles/profile_test_util.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface_iterator.h"
#include "chrome/browser/ui/extensions/extension_action_test_helper.h"
#include "chrome/browser/ui/navigator/browser_navigator.h"
#include "chrome/browser/ui/navigator/browser_navigator_params.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/extensions/extension_popup.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/contents_web_view.h"
#include "chrome/browser/ui/views/frame/multi_contents_view.h"
#include "chrome/browser/ui/views/frame/vertical_tab_strip_region_view.h"
#include "components/performance_manager/public/graph/page_node.h"
#include "components/performance_manager/public/performance_manager.h"
#include "components/prefs/pref_service.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "extensions/common/extension.h"
#include "extensions/test/test_extension_dir.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/base/base_window.h"
#include "ui/base/page_transition_types.h"
#include "ui/base/window_open_disposition.h"
#include "ui/color/color_id.h"
#include "ui/color/color_provider.h"
#include "ui/color/color_provider_key.h"
#include "ui/compositor/layer.h"
#include "ui/events/keycodes/keyboard_codes.h"
#import "ui/events/test/cocoa_test_event_utils.h"
#include "ui/gfx/color_utils.h"
#include "ui/gfx/geometry/point.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/skia_conversions.h"
#include "ui/native_theme/native_theme.h"
#include "url/gurl.h"
#include "uw/browser/tab_organization/commands.h"
#include "uw/browser/tab_organization/window_screenshot.h"
#include "uw/browser/ui/views/tab_organization/window_commands.h"
#include "uw/browser/workspaces/workspace_list.h"
#include "uw/browser/workspaces/workspace_theme.h"
#include "uw/browser/workspaces/workspace_window.h"
#include "uw/components/ai_providers/providers.h"
#include "uw/components/look/theme.h"
#include "uw/components/look/tint.h"

namespace uw {
namespace {

constexpr char kForm[] = "data:text/html,<input id=field>";
constexpr char kNotes[] = "chrome://version/?notes";
constexpr char kArticle[] = "chrome://version/?article";
constexpr char kLater[] = "chrome://version/?later";
constexpr char kAside[] = "chrome://version/?aside";
constexpr char kGreen[] =
    "data:text/html,<body style='background:rgb(40,160,90)'>";
constexpr char kBlue[] =
    "data:text/html,<body style='background:rgb(40,90,200)'>";
constexpr char kOtherProfile[] = "Other";
constexpr NSEventModifierFlags kOptionCmd =
    NSEventModifierFlagOption | NSEventModifierFlagCommand;

class WorkspacesBrowserTest : public headless::HeadlessModeBrowserTest {
 protected:
  static WorkspaceList& list() { return WorkspaceList::Get(); }

  Profile* profile() { return browser()->GetProfile(); }

  static std::string AddWorkspace(const std::string& name, Profile* profile) {
    return list().Add(name, std::string(list().UnusedIcon()),
                      profile->GetBaseName());
  }

  static NSWindow* NSWindowOf(BrowserWindowInterface* window) {
    return window->GetWindow()->GetNativeWindow().GetNativeNSWindow();
  }

  // Whether the Window menu has a row for `window`.
  static bool Listed(BrowserWindowInterface* window) {
    for (NSMenuItem* item in NSApp.windowsMenu.itemArray) {
      if (item.target == NSWindowOf(window)) {
        return true;
      }
    }
    return false;
  }

  // The Window menu's rows, for a failure message.
  static std::string WindowMenu() {
    std::string rows;
    for (NSMenuItem* item in NSApp.windowsMenu.itemArray) {
      rows += base::SysNSStringToUTF8(item.title) + " -> " +
              base::SysNSStringToUTF8(NSStringFromClass([item.target class])) +
              "\n";
    }
    return rows;
  }

  static bool IsGreen(SkColor color) {
    return SkColorGetG(color) > SkColorGetR(color) + 60 &&
           SkColorGetG(color) > SkColorGetB(color) + 30;
  }

  // Whether `color` is kGreen, kBlue, or a fade between them.
  static bool IsPageColor(SkColor color) {
    const float toward_blue = (160.0f - SkColorGetG(color)) / 70.0f;
    const float blue = 90.0f + 110.0f * toward_blue;
    return std::abs(static_cast<int>(SkColorGetR(color)) - 40) < 30 &&
           toward_blue > -0.2f && toward_blue < 1.2f &&
           std::abs(SkColorGetB(color) - blue) < 30;
  }

  // Where `window`'s page is, in the window.
  static gfx::Rect PageArea(BrowserWindowInterface* window) {
    views::View* page =
        BrowserView::GetBrowserViewForBrowser(window)->multi_contents_view();
    return page->ConvertRectToWidget(page->GetLocalBounds());
  }

  // The middle of `area` and points 24px in from its corners.
  static std::vector<gfx::Point> InsetCorners(gfx::Rect area) {
    area.Inset(24);
    return {area.CenterPoint(), area.origin(), area.top_right(),
            area.bottom_left(), area.bottom_right()};
  }

  static void Wait(base::TimeDelta time) {
    base::RunLoop loop;
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
        FROM_HERE, loop.QuitClosure(), time);
    loop.Run();
  }

  // Hands a key to `window` the way macOS hands it a shortcut.
  static void Press(BrowserWindowInterface* window,
                    ui::KeyboardCode key,
                    NSEventModifierFlags modifiers) {
    NSWindow* ns_window = NSWindowOf(window);
    [ns_window
        performKeyEquivalent:cocoa_test_event_utils::SynthesizeKeyEvent(
                                 ns_window, /*keyDown=*/true, key, modifiers)];
  }

  // The window of `member`'s group that shows, once it shows `workspace`.
  static BrowserWindowInterface* WaitForShown(BrowserWindowInterface* member,
                                              const std::string& workspace) {
    BrowserWindowInterface* shown = nullptr;
    EXPECT_TRUE(base::test::RunUntil([&] {
      std::vector<WorkspaceWindow*> group =
          WorkspaceWindow::From(member)->Partners();
      group.push_back(WorkspaceWindow::From(member));
      for (WorkspaceWindow* window : group) {
        if (window->workspace() == workspace && !window->hidden() &&
            window->window()->GetWindow()->IsVisible()) {
          shown = window->window();
          return true;
        }
      }
      return false;
    }));
    return shown;
  }

  static content::WebContents* PageOf(BrowserWindowInterface* window) {
    return window->GetActiveTabInterface()->GetContents();
  }

  static bool IsDark(content::WebContents* page) {
    return content::EvalJs(page,
                           "matchMedia('(prefers-color-scheme: dark)').matches")
        .ExtractBool();
  }

  static void Load(BrowserWindowInterface* window, const GURL& url) {
    ASSERT_TRUE(content::NavigateToURL(PageOf(window), url));
  }

  // Opens `url` in a new tab in front, where it loads as a shown page does. A
  // background tab's page loads late on a busy Mac.
  static void AddTab(BrowserWindowInterface* window, const GURL& url) {
    NavigateParams params(window, url, ui::PAGE_TRANSITION_LINK);
    params.disposition = WindowOpenDisposition::NEW_FOREGROUND_TAB;
    Navigate(&params);
    ASSERT_TRUE(
        content::WaitForLoadStop(params.navigated_or_inserted_contents));
  }

  static content::WebContents* TabOf(BrowserWindowInterface* window,
                                     const GURL& url) {
    TabStripModel* tabs = window->GetTabStripModel();
    for (int i = 0; i < tabs->count(); ++i) {
      if (tabs->GetWebContentsAt(i)->GetVisibleURL() == url) {
        return tabs->GetWebContentsAt(i);
      }
    }
    return nullptr;
  }

  static GURL Committed(content::WebContents* contents) {
    return contents->GetPrimaryMainFrame()->GetLastCommittedURL();
  }

  // Whether a restored tab has started to load its page.
  static bool Started(content::WebContents* contents) {
    return contents->GetController().GetPendingEntry() ||
           !Committed(contents).is_empty();
  }

  // Whether Chromium's loader of restored tabs took a restored tab to load
  // in the background. It gives the tab's page its address straight away.
  static bool Queued(content::WebContents* contents) {
    return !performance_manager::PerformanceManager::
                GetPrimaryPageNodeForWebContents(contents)
                    ->GetMainFrameUrl()
                    .is_empty();
  }

  static std::vector<BrowserWindowInterface*> Windows() {
    return GetAllBrowserWindowInterfaces();
  }

  // Closes a group whose window for `reading`, on kArticle, shows in front of
  // the first window, on kNotes. Another window keeps the browser open, does
  // Cmd+Shift+T, and becomes browser().
  Browser* CloseGroup(const std::string& reading) {
    Load(browser(), GURL(kNotes));
    Browser* elsewhere = CreateBrowser(profile());
    WorkspaceWindow::From(browser())->Switch(reading);
    BrowserWindowInterface* partner = WaitForShown(browser(), reading);
    CHECK(partner);
    Load(partner, GURL(kArticle));
    chrome::CloseWindow(partner->GetBrowserForMigrationOnly());
    EXPECT_TRUE(base::test::RunUntil([&] { return Windows().size() == 1u; }));
    SetBrowser(elsewhere);
    return elsewhere;
  }

  // Loads `page` in browser() and switches it to a new workspace, so it hides
  // behind the partner this returns.
  BrowserWindowInterface* SwitchAway(const GURL& page) {
    Load(browser(), page);
    const std::string reading = AddWorkspace("Reading", profile());
    WorkspaceWindow::From(browser())->Switch(reading);
    BrowserWindowInterface* partner = WaitForShown(browser(), reading);
    CHECK(partner);
    return partner;
  }

  // Checks that `window`, shown by something besides its group, took
  // `partner`'s place and shows its page.
  static void ExpectShownInsteadOf(BrowserWindowInterface* window,
                                   BrowserWindowInterface* partner) {
    EXPECT_EQ(WaitForShown(window, WorkspaceWindow::From(window)->workspace()),
              window);
    EXPECT_TRUE(base::test::RunUntil(
        [&] { return !partner->GetWindow()->IsVisible(); }));
    EXPECT_TRUE(WorkspaceWindow::From(partner)->hidden());
    EXPECT_FALSE(NSWindowOf(window).contentView.hidden);
    EXPECT_TRUE(base::test::RunUntil([&] {
      return PageOf(window)->GetVisibility() == content::Visibility::VISIBLE;
    }));
    EXPECT_FALSE(PageOf(window)->GetController().NeedsReload());
  }

  // Whether a tab of `window` shows `url`.
  static bool HasTab(BrowserWindowInterface* window, const GURL& url) {
    TabStripModel* tabs = window->GetTabStripModel();
    for (int i = 0; i < tabs->count(); ++i) {
      if (tabs->GetWebContentsAt(i)->GetVisibleURL() == url) {
        return true;
      }
    }
    return false;
  }
};

IN_PROC_BROWSER_TEST_F(WorkspacesBrowserTest, EachProfileStartsAsAWorkspace) {
  const WorkspaceWindow* window = WorkspaceWindow::From(browser());
  ASSERT_TRUE(window);
  const WorkspaceList::Workspace* workspace = list().Find(window->workspace());
  ASSERT_TRUE(workspace);
  EXPECT_EQ(workspace->profile, profile()->GetBaseName());
  EXPECT_FALSE(workspace->icon.empty());
  EXPECT_FALSE(window->hidden());
}

// Each workspace keeps its own theme, its mode included, and the pages in its
// windows see that mode.
IN_PROC_BROWSER_TEST_F(WorkspacesBrowserTest, TwoWorkspacesShowTwoThemes) {
  const std::string reading = AddWorkspace("Reading", profile());
  const std::vector<std::vector<SkColor>> suggested = SuggestedThemes();
  Theme butter;
  butter.colors = suggested[3];
  butter.mode = ThemeMode::kLight;
  SetThemeOfWindow(browser(), butter);
  Load(browser(), GURL(kForm));

  RunWindowCommand(browser(), kGoToWorkspace1 + 1);
  BrowserWindowInterface* partner = WaitForShown(browser(), reading);
  ASSERT_TRUE(partner);
  Theme wine;
  wine.colors = suggested[5];
  wine.mode = ThemeMode::kDark;
  wine.colors_in_dark_mode = true;
  SetThemeOfWindow(partner, wine);
  Load(partner, GURL(kForm));

  const auto frame = [](BrowserWindowInterface* window) {
    return BrowserView::GetBrowserViewForBrowser(window)
        ->GetWidget()
        ->GetColorProvider()
        ->GetColor(ui::kColorFrameActive);
  };
  const auto dark = [](BrowserWindowInterface* window) {
    return content::EvalJs(PageOf(window),
                           "matchMedia('(prefers-color-scheme: dark)')"
                           ".matches")
        .ExtractBool();
  };
  EXPECT_EQ(ThemeOfWindow(browser()), butter);
  EXPECT_EQ(ThemeOfWindow(partner), wine);
  EXPECT_EQ(frame(browser()), TintFrom(suggested[3][0]).start);
  EXPECT_EQ(frame(partner), TintFrom(DarkModeColorOf(suggested[5][0])).start);
  EXPECT_FALSE(dark(browser()));
  EXPECT_TRUE(dark(partner));
  SaveWindowScreenshot(partner->GetBrowserForMigrationOnly(),
                       "workspace-dark-wine");

  // A new workspace starts on a suggested theme no workspace uses.
  EXPECT_EQ(NewWorkspaceTheme(wine).colors, suggested[0]);
  EXPECT_EQ(NewWorkspaceTheme(wine).mode, ThemeMode::kDark);
}

// Per docs/plans/themes.md#the-theme-apart-from-chromium.
IN_PROC_BROWSER_TEST_F(WorkspacesBrowserTest,
                       EverythingButTheSidebarTakesThePageMode) {
  const auto colors = [&] {
    return BrowserView::GetBrowserViewForBrowser(browser())->GetColorProvider();
  };
  Theme theme;
  theme.mode = ThemeMode::kLight;
  SetThemeOfWindow(browser(), theme);
  const SkColor light_frame = colors()->GetColor(ui::kColorFrameActive);
  theme.page_mode = PageMode::kDark;
  SetThemeOfWindow(browser(), theme);
  EXPECT_EQ(colors()->GetColor(ui::kColorFrameActive), light_frame);
  EXPECT_TRUE(
      color_utils::IsDark(colors()->GetColor(ui::kColorDialogBackground)));
  Load(browser(), GURL(kForm));
  EXPECT_TRUE(IsDark(PageOf(browser())));
  AddTab(browser(), GURL("chrome://chat"));
  content::WebContents* chat = PageOf(browser());
  EXPECT_TRUE(IsDark(chat));
  SaveWindowScreenshot(browser()->GetBrowserForMigrationOnly(),
                       "page-mode-light-theme-dark-pages");
  AddTab(browser(), GURL(kNotes));
  EXPECT_TRUE(IsDark(PageOf(browser())));
  SaveWindowScreenshot(browser()->GetBrowserForMigrationOnly(),
                       "page-mode-dark-chromium-page");

  theme.mode = ThemeMode::kDark;
  theme.page_mode = PageMode::kLight;
  SetThemeOfWindow(browser(), theme);
  EXPECT_EQ(colors()->GetColor(ui::kColorFrameActive), SK_ColorBLACK);
  EXPECT_FALSE(
      color_utils::IsDark(colors()->GetColor(ui::kColorDialogBackground)));
  EXPECT_TRUE(base::test::RunUntil([&] { return !IsDark(PageOf(browser())); }));
  EXPECT_FALSE(IsDark(chat));

  // Private windows follow macOS, as in Chromium.
  Browser* incognito = CreateIncognitoBrowser();
  ASSERT_TRUE(content::NavigateToURL(PageOf(incognito), GURL(kForm)));
  EXPECT_EQ(IsDark(PageOf(incognito)),
            ui::NativeTheme::GetInstanceForWeb()->preferred_color_scheme() ==
                ui::NativeTheme::PreferredColorScheme::kDark);
}

// The sidebar looks the same whatever the page mode: every color it draws is
// the theme's.
IN_PROC_BROWSER_TEST_F(WorkspacesBrowserTest,
                       TheSidebarKeepsTheThemeUnderAnyPageMode) {
  Load(browser(), GURL(kNotes));
  AddTab(browser(), GURL(kForm));
  const auto sidebar = [&] {
    BrowserView* view = BrowserView::GetBrowserViewForBrowser(browser());
    views::View* strip = view->vertical_tab_strip_region_view_for_testing();
    const SkBitmap window =
        CaptureWindow(browser()->GetBrowserForMigrationOnly());
    const float scale = static_cast<float>(window.width()) /
                        view->GetWidget()->GetWindowBoundsInScreen().width();
    const gfx::Rect area = gfx::ScaleToEnclosedRect(
        strip->ConvertRectToWidget(strip->GetLocalBounds()), scale);
    SkBitmap pixels;
    EXPECT_TRUE(window.extractSubset(&pixels, gfx::RectToSkIRect(area)));
    return pixels;
  };
  const auto differing = [](const SkBitmap& a, const SkBitmap& b) {
    int count = 0;
    for (int y = 0; y < std::min(a.height(), b.height()); ++y) {
      for (int x = 0; x < std::min(a.width(), b.width()); ++x) {
        const SkColor p = a.getColor(x, y);
        const SkColor q = b.getColor(x, y);
        const auto apart = [](U8CPU a, U8CPU b) {
          return std::abs(static_cast<int>(a) - static_cast<int>(b)) > 2;
        };
        if (apart(SkColorGetR(p), SkColorGetR(q)) ||
            apart(SkColorGetG(p), SkColorGetG(q)) ||
            apart(SkColorGetB(p), SkColorGetB(q))) {
          ++count;
        }
      }
    }
    return count;
  };
  for (const ThemeMode mode : {ThemeMode::kLight, ThemeMode::kDark}) {
    Theme theme;
    theme.mode = mode;
    SetThemeOfWindow(browser(), theme);
    const SkBitmap same = sidebar();
    theme.page_mode =
        mode == ThemeMode::kLight ? PageMode::kDark : PageMode::kLight;
    SetThemeOfWindow(browser(), theme);
    const SkBitmap apart = sidebar();
    const int count = differing(same, apart);
    if (count) {
      const std::string name = mode == ThemeMode::kLight ? "light" : "dark";
      SaveBitmap(same, "sidebar-" + name + "-same");
      SaveBitmap(apart, "sidebar-" + name + "-apart");
    }
    EXPECT_EQ(count, 0) << "theme " << static_cast<int>(mode);
    EXPECT_EQ(same.width(), apart.width());
  }
}

IN_PROC_BROWSER_TEST_F(WorkspacesBrowserTest, TwoWorkspacesKeepTwoPageModes) {
  const std::string reading = AddWorkspace("Reading", profile());
  Theme dark_pages;
  dark_pages.mode = ThemeMode::kLight;
  dark_pages.page_mode = PageMode::kDark;
  SetThemeOfWindow(browser(), dark_pages);
  Load(browser(), GURL(kForm));
  EXPECT_EQ(NewWorkspaceTheme(dark_pages).page_mode, PageMode::kDark);

  RunWindowCommand(browser(), kGoToWorkspace1 + 1);
  BrowserWindowInterface* partner = WaitForShown(browser(), reading);
  ASSERT_TRUE(partner);
  Theme light_pages = dark_pages;
  light_pages.page_mode = PageMode::kLight;
  SetThemeOfWindow(partner, light_pages);
  Load(partner, GURL(kForm));

  EXPECT_TRUE(IsDark(PageOf(browser())));
  EXPECT_FALSE(IsDark(PageOf(partner)));
  EXPECT_EQ(ThemeOfWindow(browser()).page_mode, PageMode::kDark);
  EXPECT_EQ(ThemeOfWindow(partner).page_mode, PageMode::kLight);
}

IN_PROC_BROWSER_TEST_F(WorkspacesBrowserTest, OpenPagesTakeANewPageModeAtOnce) {
  Theme theme;
  theme.mode = ThemeMode::kLight;
  SetThemeOfWindow(browser(), theme);
  Load(browser(), GURL(kForm));
  AddTab(browser(), GURL(kGreen));
  content::WebContents* behind = TabOf(browser(), GURL(kForm));
  ASSERT_TRUE(behind);
  EXPECT_FALSE(IsDark(PageOf(browser())));

  theme.page_mode = PageMode::kDark;
  SetThemeOfWindow(browser(), theme);
  EXPECT_TRUE(base::test::RunUntil([&] { return IsDark(PageOf(browser())); }));
  EXPECT_TRUE(base::test::RunUntil([&] { return IsDark(behind); }));
  EXPECT_EQ(ThemeOfWindow(browser()).page_mode, PageMode::kDark);

  theme.page_mode = PageMode::kTheme;
  SetThemeOfWindow(browser(), theme);
  EXPECT_TRUE(base::test::RunUntil([&] { return !IsDark(PageOf(browser())); }));
}

// Extensions' pages follow page mode wherever they show, per T191.
IN_PROC_BROWSER_TEST_F(WorkspacesBrowserTest,
                       AnExtensionsPopupTakesThePageMode) {
  extensions::TestExtensionDir dir;
  dir.WriteManifest(R"({"name": "Popup", "version": "1",
                        "manifest_version": 3,
                        "action": {"default_popup": "popup.html"}})");
  dir.WriteFile(FILE_PATH_LITERAL("popup.html"), "<p>Popup</p>");
  scoped_refptr<const extensions::Extension> extension =
      extensions::ChromeTestExtensionLoader(profile()).LoadExtension(
          dir.UnpackedPath());
  ASSERT_TRUE(extension);

  Theme theme;
  theme.mode = ThemeMode::kLight;
  theme.page_mode = PageMode::kDark;
  SetThemeOfWindow(browser(), theme);
  std::unique_ptr<ExtensionActionTestHelper> actions =
      ExtensionActionTestHelper::Create(browser());
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return actions->HasAction(extension->id()); }));
  actions->Press(extension->id());
  ASSERT_TRUE(base::test::RunUntil([&] {
    ExtensionPopup* popup = ExtensionPopup::last_popup_for_testing();
    return popup && popup->host()->has_loaded_once();
  }));
  content::WebContents* popup =
      ExtensionPopup::last_popup_for_testing()->host()->host_contents();
  EXPECT_TRUE(IsDark(popup));
}

// Before a page paints, the card behind it is black or white by page mode.
IN_PROC_BROWSER_TEST_F(WorkspacesBrowserTest,
                       TheCardBehindAPageTakesThePageMode) {
  const auto card = [&] {
    return BrowserView::GetBrowserViewForBrowser(browser())
        ->contents_web_view()
        ->layer()
        ->AsSolidColor()
        ->GetTargetColor()
        .toSkColor();
  };
  Theme theme;
  theme.mode = ThemeMode::kLight;
  theme.page_mode = PageMode::kDark;
  SetThemeOfWindow(browser(), theme);
  Load(browser(), GURL(kForm));
  EXPECT_EQ(card(), kBlackCard);
  Load(browser(), GURL(kNotes));
  EXPECT_EQ(card(), kBlackCard);

  theme.mode = ThemeMode::kDark;
  theme.page_mode = PageMode::kLight;
  SetThemeOfWindow(browser(), theme);
  Load(browser(), GURL(kForm));
  EXPECT_EQ(card(), SK_ColorWHITE);

  theme.page_mode = PageMode::kTheme;
  SetThemeOfWindow(browser(), theme);
  EXPECT_NE(card(), SK_ColorWHITE);
}

IN_PROC_BROWSER_TEST_F(WorkspacesBrowserTest, SwitchingSwapsTheWindowInPlace) {
  const std::string personal = WorkspaceWindow::From(browser())->workspace();
  const std::string reading = AddWorkspace("Reading", profile());
  const gfx::Rect bounds(80, 60, 900, 640);
  browser()->GetWindow()->SetBounds(bounds);
  Load(browser(), GURL(kForm));
  ASSERT_TRUE(content::ExecJs(PageOf(browser()),
                              "document.getElementById('field').value = "
                              "'half typed'"));

  RunWindowCommand(browser(), kGoToWorkspace1 + 1);
  BrowserWindowInterface* partner = WaitForShown(browser(), reading);
  ASSERT_TRUE(partner);
  EXPECT_NE(partner, browser());
  EXPECT_EQ(partner->GetProfile(), profile());
  EXPECT_EQ(partner->GetWindow()->GetBounds(), bounds);
  EXPECT_FALSE(browser()->GetWindow()->IsVisible());

  RunWindowCommand(partner, kGoToWorkspace1);
  EXPECT_EQ(WaitForShown(browser(), personal), browser());
  EXPECT_FALSE(partner->GetWindow()->IsVisible());
  EXPECT_EQ(content::EvalJs(PageOf(browser()),
                            "document.getElementById('field').value"),
            "half typed");
}

// Cmd+Option+arrows no longer pick the next tab, and Cmd+Shift+] still does.
IN_PROC_BROWSER_TEST_F(WorkspacesBrowserTest, KeysRunTheWorkspaceCommands) {
  NSWindow* window =
      browser()->GetWindow()->GetNativeWindow().GetNativeNSWindow();
  const auto command = [&](ui::KeyboardCode key,
                           NSEventModifierFlags modifiers) {
    return CommandForKeyEvent(cocoa_test_event_utils::SynthesizeKeyEvent(
                                  window, /*keyDown=*/true, key, modifiers))
        .chrome_command;
  };
  EXPECT_EQ(command(ui::VKEY_1, NSEventModifierFlagControl), kGoToWorkspace1);
  EXPECT_EQ(command(ui::VKEY_9, NSEventModifierFlagControl), kGoToWorkspace9);
  EXPECT_EQ(command(ui::VKEY_RIGHT, kOptionCmd), kNextWorkspace);
  EXPECT_EQ(command(ui::VKEY_LEFT, kOptionCmd), kPreviousWorkspace);
  EXPECT_EQ(command(ui::VKEY_OEM_6,
                    NSEventModifierFlagCommand | NSEventModifierFlagShift),
            IDC_SELECT_NEXT_TAB);

  const std::string reading = AddWorkspace("Reading", profile());
  Press(browser(), ui::VKEY_2, NSEventModifierFlagControl);
  EXPECT_TRUE(WaitForShown(browser(), reading));
}

IN_PROC_BROWSER_TEST_F(WorkspacesBrowserTest, CmdOptionArrowsSwitchInOrder) {
  AddWorkspace("Reading", profile());
  AddWorkspace("Side project", profile());
  const std::string second = list().workspaces()[1].id;
  const std::string third = list().workspaces()[2].id;

  Press(browser(), ui::VKEY_RIGHT, kOptionCmd);
  BrowserWindowInterface* reading = WaitForShown(browser(), second);
  ASSERT_TRUE(reading);
  Press(reading, ui::VKEY_RIGHT, kOptionCmd);
  BrowserWindowInterface* side_project = WaitForShown(browser(), third);
  ASSERT_TRUE(side_project);
  // Past the last workspace, nothing changes.
  EXPECT_FALSE(CanRunWindowCommand(side_project, kNextWorkspace));
  Press(side_project, ui::VKEY_LEFT, kOptionCmd);
  EXPECT_EQ(WaitForShown(browser(), second), reading);
}

IN_PROC_BROWSER_TEST_F(WorkspacesBrowserTest,
                       OnlyWorkspacesOnOneProfileShareSignIns) {
  ASSERT_TRUE(embedded_test_server()->Start());
  ProfileManager* manager = g_browser_process->profile_manager();
  Profile& other = profiles::testing::CreateProfileSync(
      manager, manager->GenerateNextProfileDirectoryPath());
  const std::string reading = AddWorkspace("Reading", profile());
  const std::string work = AddWorkspace("Work", &other);
  const GURL signed_in =
      embedded_test_server()->GetURL("/set-cookie?signed=in");
  Load(browser(), signed_in);

  WorkspaceWindow::From(browser())->Switch(reading);
  BrowserWindowInterface* same = WaitForShown(browser(), reading);
  ASSERT_TRUE(same);
  EXPECT_EQ(content::GetCookies(same->GetProfile(), signed_in), "signed=in");

  WorkspaceWindow::From(same)->Switch(work);
  BrowserWindowInterface* apart = WaitForShown(browser(), work);
  ASSERT_TRUE(apart);
  EXPECT_EQ(apart->GetProfile(), &other);
  EXPECT_EQ(apart->GetWindow()->GetBounds(), same->GetWindow()->GetBounds());
  EXPECT_EQ(content::GetCookies(apart->GetProfile(), signed_in), "");
}

IN_PROC_BROWSER_TEST_F(WorkspacesBrowserTest, CmdNOpensInTheCurrentWorkspace) {
  const std::string reading = AddWorkspace("Reading", profile());
  WorkspaceWindow::From(browser())->Switch(reading);
  BrowserWindowInterface* partner = WaitForShown(browser(), reading);
  ASSERT_TRUE(partner);
  const std::vector<BrowserWindowInterface*> before = Windows();

  chrome::ExecuteCommand(partner->GetBrowserForMigrationOnly(), IDC_NEW_WINDOW);
  BrowserWindowInterface* opened = nullptr;
  ASSERT_TRUE(base::test::RunUntil([&] {
    for (BrowserWindowInterface* window : Windows()) {
      if (!std::ranges::contains(before, window)) {
        opened = window;
      }
    }
    return opened != nullptr;
  }));
  EXPECT_EQ(WorkspaceWindow::From(opened)->workspace(), reading);
  EXPECT_FALSE(std::ranges::contains(WorkspaceWindow::From(partner)->Partners(),
                                     WorkspaceWindow::From(opened)));
}

IN_PROC_BROWSER_TEST_F(WorkspacesBrowserTest,
                       ShowingAHiddenWindowSwitchesToIt) {
  const std::string personal = WorkspaceWindow::From(browser())->workspace();
  const std::string reading = AddWorkspace("Reading", profile());
  WorkspaceWindow::From(browser())->Switch(reading);
  BrowserWindowInterface* partner = WaitForShown(browser(), reading);
  ASSERT_TRUE(partner);

  // As when a link from another app opens in the hidden window.
  browser()->GetWindow()->Show();
  EXPECT_EQ(WaitForShown(browser(), personal), browser());
  EXPECT_FALSE(partner->GetWindow()->IsVisible());
}

// The command bar's open-tab rows come from these: every window of the
// profile, hidden ones too, and no other profile's.
IN_PROC_BROWSER_TEST_F(WorkspacesBrowserTest,
                       OpenTabsComeFromEveryWorkspaceOfTheProfile) {
  ProfileManager* manager = g_browser_process->profile_manager();
  Profile& other = profiles::testing::CreateProfileSync(
      manager, manager->GenerateNextProfileDirectoryPath());
  const std::string reading = AddWorkspace("Reading", profile());
  const std::string work = AddWorkspace("Work", &other);
  WorkspaceWindow::From(browser())->Switch(reading);
  BrowserWindowInterface* partner = WaitForShown(browser(), reading);
  ASSERT_TRUE(partner);
  Load(partner, GURL(kArticle));
  WorkspaceWindow::From(partner)->Switch(work);
  BrowserWindowInterface* apart = WaitForShown(browser(), work);
  ASSERT_TRUE(apart);
  Load(apart, GURL(kNotes));

  std::vector<GURL> found;
  for (const TabMatcher::TabWrapper& tab :
       TabMatcherDesktop(nullptr, profile())
           .GetOpenTabs(nullptr, /*exclude_active_tab=*/false)) {
    found.push_back(tab.url);
  }
  EXPECT_TRUE(std::ranges::contains(found, GURL(kArticle)));
  EXPECT_FALSE(std::ranges::contains(found, GURL(kNotes)));
}

// A provider saved in one profile is in every profile, and each keeps its own
// model choices.
IN_PROC_BROWSER_TEST_F(WorkspacesBrowserTest,
                       ProvidersAreSharedByEveryProfile) {
  PrefService* prefs = profile()->GetPrefs();
  const std::string id =
      AddProvider(*prefs, PresetProvider(ProviderPreset::kOllama));
  ProfileManager* manager = g_browser_process->profile_manager();
  Profile& other = profiles::testing::CreateProfileSync(
      manager, manager->GenerateNextProfileDirectoryPath());
  EXPECT_TRUE(FindProvider(*other.GetPrefs(), id));

  SetChoice(*other.GetPrefs(), ModelSlot::kLanguage, id, "llama3");
  EXPECT_FALSE(ReadChoice(*prefs, ModelSlot::kLanguage));

  RemoveProvider(*other.GetPrefs(), id);
  EXPECT_FALSE(FindProvider(*prefs, id));
}

IN_PROC_BROWSER_TEST_F(WorkspacesBrowserTest,
                       PRE_ARemovedProviderStaysRemovedInAProfileNotLoaded) {
  AddProvider(*profile()->GetPrefs(), PresetProvider(ProviderPreset::kOllama));
  ProfileManager* manager = g_browser_process->profile_manager();
  Profile& other = profiles::testing::CreateProfileSync(
      manager, manager->user_data_dir().AppendASCII(kOtherProfile));
  EXPECT_EQ(ReadProviders(*other.GetPrefs()).size(), 1u);
}

// The other profile still has the provider when it loads, but has joined
// before, so it takes the shared providers.
IN_PROC_BROWSER_TEST_F(WorkspacesBrowserTest,
                       ARemovedProviderStaysRemovedInAProfileNotLoaded) {
  ProfileManager* manager = g_browser_process->profile_manager();
  const base::FilePath path =
      manager->user_data_dir().AppendASCII(kOtherProfile);
  ASSERT_FALSE(manager->GetProfileByPath(path));
  const std::vector<Provider> providers = ReadProviders(*profile()->GetPrefs());
  ASSERT_EQ(providers.size(), 1u);
  RemoveProvider(*profile()->GetPrefs(), providers[0].id);

  Profile& other = profiles::testing::CreateProfileSync(manager, path);
  EXPECT_TRUE(ReadProviders(*other.GetPrefs()).empty());
  EXPECT_TRUE(ReadProviders(*profile()->GetPrefs()).empty());
}

// As when the command bar's row for an open tab is picked.
IN_PROC_BROWSER_TEST_F(WorkspacesBrowserTest,
                       SwitchingToATabInAnotherWorkspaceShowsIt) {
  const std::string personal = WorkspaceWindow::From(browser())->workspace();
  const std::string reading = AddWorkspace("Reading", profile());
  Load(browser(), GURL(kNotes));
  WorkspaceWindow::From(browser())->Switch(reading);
  BrowserWindowInterface* partner = WaitForShown(browser(), reading);
  ASSERT_TRUE(partner);
  Load(partner, GURL(kArticle));

  NavigateParams params(partner, GURL(kNotes),
                        ui::PAGE_TRANSITION_AUTO_BOOKMARK);
  params.disposition = WindowOpenDisposition::SWITCH_TO_TAB;
  params.source_contents = PageOf(partner);
  Navigate(&params);
  EXPECT_EQ(WaitForShown(browser(), personal), browser());
  EXPECT_EQ(PageOf(browser())->GetVisibleURL(), GURL(kNotes));
}

// As an extension's windows.create({focused: false}) does.
IN_PROC_BROWSER_TEST_F(WorkspacesBrowserTest,
                       AHiddenWindowShownInactiveTakesItsGroupsPlace) {
  BrowserWindowInterface* partner = SwitchAway(GURL(kNotes));
  browser()->GetWindow()->ShowInactive();
  ExpectShownInsteadOf(browser(), partner);
}

// macOS's Window menu, the Dock, and Cmd+` show a window past BrowserView.
IN_PROC_BROWSER_TEST_F(WorkspacesBrowserTest,
                       AHiddenWindowMacOSShowsTakesItsGroupsPlace) {
  BrowserWindowInterface* partner = SwitchAway(GURL(kNotes));
  [NSWindowOf(browser()) makeKeyAndOrderFront:nil];
  ExpectShownInsteadOf(browser(), partner);
}

IN_PROC_BROWSER_TEST_F(WorkspacesBrowserTest, PrivateWindowsHaveNoWorkspace) {
  AddWorkspace("Reading", profile());
  Browser* incognito = CreateIncognitoBrowser();
  EXPECT_FALSE(WorkspaceWindow::From(incognito));
  EXPECT_FALSE(CanRunWindowCommand(incognito, kGoToWorkspace1 + 1));
  EXPECT_FALSE(CanRunWindowCommand(incognito, kNextWorkspace));
}

IN_PROC_BROWSER_TEST_F(WorkspacesBrowserTest,
                       ClosingAWindowClosesItsWorkspacesUntilCmdShiftT) {
  const std::string personal = WorkspaceWindow::From(browser())->workspace();
  const std::string reading = AddWorkspace("Reading", profile());
  Browser* elsewhere = CloseGroup(reading);
  ASSERT_EQ(Windows().size(), 1u);
  EXPECT_EQ(Windows()[0], elsewhere);

  chrome::RestoreTab(elsewhere);
  BrowserWindowInterface* shown = nullptr;
  BrowserWindowInterface* behind = nullptr;
  ASSERT_TRUE(base::test::RunUntil([&] {
    shown = behind = nullptr;
    for (BrowserWindowInterface* window : Windows()) {
      const WorkspaceWindow* workspace = WorkspaceWindow::From(window);
      if (workspace->workspace() == reading && !workspace->hidden() &&
          window->GetWindow()->IsVisible()) {
        shown = window;
      } else if (workspace->workspace() == personal && workspace->hidden() &&
                 !window->GetWindow()->IsVisible()) {
        behind = window;
      }
    }
    return shown && behind;
  }));
  EXPECT_TRUE(std::ranges::contains(WorkspaceWindow::From(shown)->Partners(),
                                    WorkspaceWindow::From(behind)));
  EXPECT_TRUE(HasTab(shown, GURL(kArticle)));
  EXPECT_TRUE(HasTab(behind, GURL(kNotes)));
  EXPECT_FALSE(Started(PageOf(behind)));
}

// Including a window of it that closed before it went.
IN_PROC_BROWSER_TEST_F(WorkspacesBrowserTest,
                       CmdShiftTDoesNotBringBackADeletedWorkspace) {
  const std::string personal = WorkspaceWindow::From(browser())->workspace();
  const std::string reading = AddWorkspace("Reading", profile());
  CloseGroup(reading);
  WorkspaceWindow::Delete(reading, /*with_profile=*/false);

  chrome::RestoreTab(browser());
  BrowserWindowInterface* restored = nullptr;
  ASSERT_TRUE(base::test::RunUntil([&] {
    for (BrowserWindowInterface* window : Windows()) {
      if (window != browser() && window->GetWindow()->IsVisible()) {
        restored = window;
      }
    }
    return restored != nullptr;
  }));
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(Windows().size(), 2u);
  EXPECT_EQ(WorkspaceWindow::From(restored)->workspace(), personal);
  EXPECT_TRUE(HasTab(restored, GURL(kNotes)));
}

// As when the workspace went while its profile was not loaded.
IN_PROC_BROWSER_TEST_F(WorkspacesBrowserTest,
                       AWindowOfAGoneWorkspaceComesBackOnItsOwn) {
  const std::string personal = WorkspaceWindow::From(browser())->workspace();
  const std::string reading = AddWorkspace("Reading", profile());
  CloseGroup(reading);
  list().Remove(reading);

  chrome::RestoreTab(browser());
  BrowserWindowInterface* article = nullptr;
  BrowserWindowInterface* notes = nullptr;
  ASSERT_TRUE(base::test::RunUntil([&] {
    article = notes = nullptr;
    for (BrowserWindowInterface* window : Windows()) {
      if (window->GetWindow()->IsVisible() && HasTab(window, GURL(kArticle))) {
        article = window;
      } else if (window->GetWindow()->IsVisible() &&
                 HasTab(window, GURL(kNotes))) {
        notes = window;
      }
    }
    return article && notes;
  }));
  const WorkspaceWindow* alone = WorkspaceWindow::From(article);
  EXPECT_FALSE(alone->hidden());
  EXPECT_EQ(alone->workspace(), personal);
  EXPECT_TRUE(alone->Partners().empty());
}

IN_PROC_BROWSER_TEST_F(WorkspacesBrowserTest,
                       PRE_EachWindowComesBackInItsWorkspace) {
  SessionStartupPref::SetStartupPref(
      profile(), SessionStartupPref(SessionStartupPref::LAST));
  const std::string reading = AddWorkspace("Reading", profile());
  Load(browser(), GURL(kLater));
  AddTab(browser(), GURL(kNotes));
  WorkspaceWindow::From(browser())->Switch(reading);
  BrowserWindowInterface* partner = WaitForShown(browser(), reading);
  ASSERT_TRUE(partner);
  Load(partner, GURL(kAside));
  AddTab(partner, GURL(kArticle));
  // Lets the session take the windows' data before the browser quits.
  base::RunLoop().RunUntilIdle();
}

IN_PROC_BROWSER_TEST_F(WorkspacesBrowserTest,
                       EachWindowComesBackInItsWorkspace) {
  ASSERT_EQ(list().workspaces().size(), 2u);
  const std::string personal = list().workspaces()[0].id;
  const std::string reading = list().workspaces()[1].id;
  BrowserWindowInterface* shown = nullptr;
  BrowserWindowInterface* behind = nullptr;
  ASSERT_TRUE(base::test::RunUntil([&] {
    shown = behind = nullptr;
    for (BrowserWindowInterface* window : Windows()) {
      const WorkspaceWindow* workspace = WorkspaceWindow::From(window);
      if (workspace->workspace() == reading && !workspace->hidden() &&
          window->GetWindow()->IsVisible()) {
        shown = window;
      } else if (workspace->workspace() == personal && workspace->hidden() &&
                 !window->GetWindow()->IsVisible()) {
        behind = window;
      }
    }
    return shown && behind && Windows().size() == 2u;
  }));
  EXPECT_TRUE(std::ranges::contains(WorkspaceWindow::From(shown)->Partners(),
                                    WorkspaceWindow::From(behind)));
  EXPECT_TRUE(HasTab(shown, GURL(kArticle)));
  EXPECT_TRUE(HasTab(behind, GURL(kNotes)));

  // Chromium's loader of restored tabs takes the shown window's background
  // tab. The hidden window's tabs wait until it shows.
  content::WebContents* aside = TabOf(shown, GURL(kAside));
  content::WebContents* notes = TabOf(behind, GURL(kNotes));
  content::WebContents* later = TabOf(behind, GURL(kLater));
  ASSERT_TRUE(aside && notes && later);
  EXPECT_TRUE(Queued(aside));
  EXPECT_FALSE(Started(notes) || Queued(notes));
  EXPECT_FALSE(Started(later) || Queued(later));

  // Switching back shows the restored window, with no third one, and loads
  // its tab. Its other tab loads once selected.
  WorkspaceWindow::From(shown)->Switch(personal);
  EXPECT_EQ(WaitForShown(shown, personal), behind);
  EXPECT_EQ(Windows().size(), 2u);
  EXPECT_TRUE(
      base::test::RunUntil([&] { return Committed(notes) == GURL(kNotes); }));
  EXPECT_FALSE(Started(later) || Queued(later));
  TabStripModel* tabs = behind->GetTabStripModel();
  tabs->ActivateTabAt(tabs->GetIndexOfWebContents(later));
  EXPECT_TRUE(
      base::test::RunUntil([&] { return Committed(later) == GURL(kLater); }));
}

// Run with pnpm run window-tests: only a real window is in macOS's Window
// menu.
IN_PROC_BROWSER_TEST_F(WorkspacesBrowserTest,
                       MANUAL_TheWindowMenuLeavesOutTheHiddenWindow) {
  BrowserWindowInterface* partner = SwitchAway(GURL(kNotes));
  [NSApp.windowsMenu update];
  EXPECT_TRUE(Listed(partner)) << WindowMenu();
  EXPECT_FALSE(Listed(browser())) << WindowMenu();

  // Window > Bring All to Front leaves it behind.
  [NSApp arrangeInFront:nil];
  base::RunLoop().RunUntilIdle();
  EXPECT_FALSE(browser()->GetWindow()->IsVisible());
  EXPECT_TRUE(partner->GetWindow()->IsVisible());
}

// Run with pnpm run window-tests: only a real window draws its page.
IN_PROC_BROWSER_TEST_F(WorkspacesBrowserTest,
                       MANUAL_AHiddenWindowMacOSShowsDrawsItsPage) {
  BrowserWindowInterface* partner = SwitchAway(GURL(kGreen));
  [NSWindowOf(browser()) makeKeyAndOrderFront:nil];
  ExpectShownInsteadOf(browser(), partner);
  SkColor middle = SK_ColorWHITE;
  for (int tries = 0; tries < 30 && !IsGreen(middle); ++tries) {
    Wait(base::Milliseconds(100));
    const SkBitmap pixels = CaptureNativeWindow(browser());
    middle = pixels.getColor(pixels.width() / 2, pixels.height() / 2);
  }
  EXPECT_TRUE(IsGreen(middle)) << std::hex << middle;
  SaveNativeWindowScreenshot(browser(), "workspace-shown-by-macos");
}

IN_PROC_BROWSER_TEST_F(WorkspacesBrowserTest,
                       ASwitchDrawsUnderTheWindowThenComesUpWithoutAnimating) {
  BrowserWindowInterface* partner = SwitchAway(GURL(kNotes));
  const std::string work = WorkspaceWindow::From(browser())->workspace();
  ASSERT_TRUE(WorkspaceWindow::From(partner)->Switch(work));
  // browser() is in, under the window that shows, drawing its page unseen.
  EXPECT_TRUE(WorkspaceWindow::From(partner)->switching());
  EXPECT_TRUE(browser()->GetWindow()->IsVisible());
  EXPECT_TRUE(WorkspaceWindow::From(browser())->hidden());
  EXPECT_FALSE(NSWindowOf(browser()).contentView.hidden);
  EXPECT_TRUE(partner->GetWindow()->IsVisible());
  EXPECT_FALSE(WorkspaceWindow::From(partner)->hidden());
  EXPECT_EQ(NSWindowOf(browser()).animationBehavior,
            NSWindowAnimationBehaviorNone);
  EXPECT_EQ(NSWindowOf(partner).animationBehavior,
            NSWindowAnimationBehaviorNone);
  // A second switch waits for this one.
  EXPECT_FALSE(WorkspaceWindow::From(partner)->Switch(work));

  EXPECT_EQ(WaitForShown(partner, work), browser());
  EXPECT_FALSE(WorkspaceWindow::From(partner)->switching());
  EXPECT_FALSE(partner->GetWindow()->IsVisible());
  EXPECT_TRUE(WorkspaceWindow::From(partner)->hidden());
  EXPECT_EQ(NSWindowOf(browser()).animationBehavior,
            NSWindowAnimationBehaviorDocumentWindow);
  EXPECT_EQ(NSWindowOf(partner).animationBehavior,
            NSWindowAnimationBehaviorDocumentWindow);
}

IN_PROC_BROWSER_TEST_F(WorkspacesBrowserTest, ANewWindowStillAnimates) {
  chrome::ExecuteCommand(browser(), IDC_NEW_WINDOW);
  ASSERT_TRUE(base::test::RunUntil([&] { return Windows().size() == 2u; }));
  for (BrowserWindowInterface* window : Windows()) {
    EXPECT_EQ(NSWindowOf(window).animationBehavior,
              NSWindowAnimationBehaviorDocumentWindow);
  }
}

// Run with pnpm run window-tests: switching each way, the window in front
// always shows one of the two pages, or the fade between them, and never a
// blank page. While both windows are on screen, the one coming waits under
// the one going, and neither plays macOS's animation. Capturing the screen
// itself, which would show that animation, needs a permission the VM has not
// granted, so each window is captured on its own.
IN_PROC_BROWSER_TEST_F(WorkspacesBrowserTest,
                       MANUAL_ASwitchNeverShowsABlankOrAnimatingWindow) {
  BrowserWindowInterface* partner = SwitchAway(GURL(kBlue));
  Load(partner, GURL(kGreen));
  const std::string work = WorkspaceWindow::From(browser())->workspace();
  const std::string reading = WorkspaceWindow::From(partner)->workspace();
  Wait(base::Milliseconds(500));

  BrowserWindowInterface* from = partner;
  int frames = 0;
  for (const std::string& to : {work, reading, work}) {
    BrowserWindowInterface* coming = from == partner ? browser() : partner;
    RunWindowCommand(from, to == work ? kPreviousWorkspace : kNextWorkspace);
    const base::TimeTicks give_up = base::TimeTicks::Now() + base::Seconds(5);
    base::TimeTicks gone;
    while (base::TimeTicks::Now() < give_up &&
           (gone.is_null() ||
            base::TimeTicks::Now() < gone + base::Milliseconds(400))) {
      NSWindow* going = NSWindowOf(from);
      NSWindow* arriving = NSWindowOf(coming);
      if (going.visible && arriving.visible) {
        EXPECT_GT(arriving.orderedIndex, going.orderedIndex);
        EXPECT_EQ(going.animationBehavior, NSWindowAnimationBehaviorNone);
        EXPECT_EQ(arriving.animationBehavior, NSWindowAnimationBehaviorNone);
      }
      BrowserWindowInterface* front =
          going.visible && (!arriving.visible ||
                            going.orderedIndex < arriving.orderedIndex)
              ? from
              : coming;
      const bool going_shown = going.visible;
      const bool arriving_shown = arriving.visible;
      const SkBitmap pixels =
          CaptureNativeWindow(front->GetBrowserForMigrationOnly());
      // A capture lands a little later, so one that spans the moment the
      // windows trade places pictures the wrong one.
      const bool settled =
          going.visible == going_shown && arriving.visible == arriving_shown;
      const double scale = pixels.width() / NSWidth(NSWindowOf(front).frame);
      const std::vector<gfx::Point> points =
          settled ? InsetCorners(PageArea(front)) : std::vector<gfx::Point>();
      for (const gfx::Point& point : points) {
        const SkColor color =
            pixels.getColor(static_cast<int>(point.x() * scale),
                            static_cast<int>(point.y() * scale));
        EXPECT_TRUE(IsPageColor(color))
            << "frame " << frames << " at " << point.ToString() << ": "
            << std::hex << color;
      }
      if (frames < 12) {
        SaveBitmap(pixels, "switch-" + base::NumberToString(frames));
      }
      ++frames;
      if (gone.is_null() && !going.visible) {
        gone = base::TimeTicks::Now();
      }
    }
    EXPECT_FALSE(gone.is_null());
    from = WaitForShown(from, to);
    ASSERT_EQ(from, coming);
    EXPECT_EQ(NSWindowOf(browser()).animationBehavior,
              NSWindowAnimationBehaviorDocumentWindow);
    EXPECT_EQ(NSWindowOf(partner).animationBehavior,
              NSWindowAnimationBehaviorDocumentWindow);
  }
}

}  // namespace
}  // namespace uw
