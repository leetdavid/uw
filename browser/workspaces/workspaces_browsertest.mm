// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#import <AppKit/AppKit.h>

#include <algorithm>
#include <string>
#include <vector>

#include "base/files/file_path.h"
#include "base/run_loop.h"
#include "base/strings/sys_string_conversions.h"
#include "base/task/single_thread_task_runner.h"
#include "base/test/run_until.h"
#include "base/time/time.h"
#include "chrome/app/chrome_command_ids.h"
#include "chrome/browser/autocomplete/tab_matcher_desktop.h"
#include "chrome/browser/browser_process.h"
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
#include "chrome/browser/ui/navigator/browser_navigator.h"
#include "chrome/browser/ui/navigator/browser_navigator_params.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "components/prefs/pref_service.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/base/base_window.h"
#include "ui/base/page_transition_types.h"
#include "ui/base/window_open_disposition.h"
#include "ui/events/keycodes/keyboard_codes.h"
#import "ui/events/test/cocoa_test_event_utils.h"
#include "ui/gfx/geometry/rect.h"
#include "url/gurl.h"
#include "uw/browser/tab_organization/commands.h"
#include "uw/browser/tab_organization/window_screenshot.h"
#include "uw/browser/ui/views/tab_organization/window_commands.h"
#include "uw/browser/workspaces/workspace_list.h"
#include "uw/browser/workspaces/workspace_window.h"
#include "uw/components/ai_providers/providers.h"

namespace uw {
namespace {

constexpr char kForm[] = "data:text/html,<input id=field>";
constexpr char kNotes[] = "chrome://version/?notes";
constexpr char kArticle[] = "chrome://version/?article";
constexpr char kLater[] = "chrome://version/?later";
constexpr char kAside[] = "chrome://version/?aside";
constexpr char kGreen[] =
    "data:text/html,<body style='background:rgb(40,160,90)'>";
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

  static void Load(BrowserWindowInterface* window, const GURL& url) {
    ASSERT_TRUE(content::NavigateToURL(PageOf(window), url));
  }

  static void AddBackgroundTab(BrowserWindowInterface* window,
                               const GURL& url) {
    NavigateParams params(window, url, ui::PAGE_TRANSITION_LINK);
    params.disposition = WindowOpenDisposition::NEW_BACKGROUND_TAB;
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
  Load(browser(), GURL(kNotes));
  AddBackgroundTab(browser(), GURL(kLater));
  WorkspaceWindow::From(browser())->Switch(reading);
  BrowserWindowInterface* partner = WaitForShown(browser(), reading);
  ASSERT_TRUE(partner);
  Load(partner, GURL(kArticle));
  AddBackgroundTab(partner, GURL(kAside));
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

  // The shown window's background tab loads, as Chromium loads restored
  // tabs. The hidden window's tabs wait until it shows.
  content::WebContents* aside = TabOf(shown, GURL(kAside));
  ASSERT_TRUE(aside);
  ASSERT_TRUE(
      base::test::RunUntil([&] { return Committed(aside) == GURL(kAside); }));
  content::WebContents* notes = TabOf(behind, GURL(kNotes));
  content::WebContents* later = TabOf(behind, GURL(kLater));
  ASSERT_TRUE(notes && later);
  EXPECT_FALSE(Started(notes));
  EXPECT_FALSE(Started(later));

  // Switching back shows the restored window, with no third one, and loads
  // its tab. Its other tab loads once selected.
  WorkspaceWindow::From(shown)->Switch(personal);
  EXPECT_EQ(WaitForShown(shown, personal), behind);
  EXPECT_EQ(Windows().size(), 2u);
  EXPECT_TRUE(
      base::test::RunUntil([&] { return Committed(notes) == GURL(kNotes); }));
  EXPECT_FALSE(Started(later));
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

}  // namespace
}  // namespace uw
