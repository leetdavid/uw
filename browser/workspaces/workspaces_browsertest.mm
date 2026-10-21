// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#import <AppKit/AppKit.h>

#include <algorithm>
#include <string>
#include <vector>

#include "base/run_loop.h"
#include "base/test/run_until.h"
#include "chrome/app/chrome_command_ids.h"
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
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "ui/base/base_window.h"
#include "ui/events/keycodes/keyboard_codes.h"
#import "ui/events/test/cocoa_test_event_utils.h"
#include "ui/gfx/geometry/rect.h"
#include "url/gurl.h"
#include "uw/browser/tab_organization/commands.h"
#include "uw/browser/ui/views/tab_organization/window_commands.h"
#include "uw/browser/workspaces/workspace_list.h"
#include "uw/browser/workspaces/workspace_window.h"

namespace uw {
namespace {

constexpr char kForm[] = "data:text/html,<input id=field>";
constexpr char kNotes[] = "chrome://version/?notes";
constexpr char kArticle[] = "chrome://version/?article";
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

  // Hands a key to `window` the way macOS hands it a shortcut.
  static void Press(BrowserWindowInterface* window,
                    ui::KeyboardCode key,
                    NSEventModifierFlags modifiers) {
    NSWindow* ns_window =
        window->GetWindow()->GetNativeWindow().GetNativeNSWindow();
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

  static std::vector<BrowserWindowInterface*> Windows() {
    return GetAllBrowserWindowInterfaces();
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
  Load(browser(), GURL(kNotes));
  // Another window keeps the browser open and does Cmd+Shift+T.
  Browser* elsewhere = CreateBrowser(profile());
  WorkspaceWindow::From(browser())->Switch(reading);
  BrowserWindowInterface* partner = WaitForShown(browser(), reading);
  ASSERT_TRUE(partner);
  Load(partner, GURL(kArticle));

  const BrowserWindowInterface* closed_personal = browser();
  chrome::CloseWindow(partner->GetBrowserForMigrationOnly());
  ASSERT_TRUE(base::test::RunUntil([&] { return Windows().size() == 1u; }));
  EXPECT_EQ(Windows()[0], elsewhere);
  EXPECT_FALSE(std::ranges::contains(Windows(), closed_personal));

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
  SetBrowser(elsewhere);
}

IN_PROC_BROWSER_TEST_F(WorkspacesBrowserTest,
                       PRE_EachWindowComesBackInItsWorkspace) {
  SessionStartupPref::SetStartupPref(
      profile(), SessionStartupPref(SessionStartupPref::LAST));
  const std::string reading = AddWorkspace("Reading", profile());
  Load(browser(), GURL(kNotes));
  WorkspaceWindow::From(browser())->Switch(reading);
  BrowserWindowInterface* partner = WaitForShown(browser(), reading);
  ASSERT_TRUE(partner);
  Load(partner, GURL(kArticle));
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

  // Switching back shows the restored window, with no third one.
  WorkspaceWindow::From(shown)->Switch(personal);
  EXPECT_EQ(WaitForShown(shown, personal), behind);
  EXPECT_EQ(Windows().size(), 2u);
}

}  // namespace
}  // namespace uw
