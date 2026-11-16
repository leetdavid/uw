// Copyright 2026 The uw Authors. All rights reserved.

#import <AppKit/AppKit.h>

#include <string>

#include "base/test/run_until.h"
#include "chrome/browser/headless/test/headless_mode_browsertest.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "url/gurl.h"
#import "uw/browser/ui/views/workspaces/move_task_menu_mac.h"
#include "uw/browser/workspaces/workspace_list.h"
#include "uw/browser/workspaces/workspace_window.h"

namespace uw {
namespace {

constexpr char kForm[] = "data:text/html,<input id=field>";

using MoveTaskMenuBrowserTest = headless::HeadlessModeBrowserTest;

bool Dimmed(NSMenuItem* item) {
  return ![(id<NSMenuItemValidation>)item.target validateMenuItem:item];
}

IN_PROC_BROWSER_TEST_F(MoveTaskMenuBrowserTest,
                       TheTasksMenuListsWorkspacesAndMovesTheTask) {
  NSMenuItem* item = [[NSMenuItem alloc] initWithTitle:@"Move Task"
                                                action:nil
                                         keyEquivalent:@""];
  item.submenu = [[NSMenu alloc] initWithTitle:@"Move Task"];
  SetUpMoveTaskMenu(item);
  content::WebContents* page =
      chrome::AddAndReturnTabAt(browser(), GURL(kForm), -1, true);
  TabStripModel* tabs = browser()->tab_strip_model();
  tabs->AddToNewGroup({tabs->GetIndexOfWebContents(page)});
  EXPECT_TRUE(Dimmed(item));

  WorkspaceList& list = WorkspaceList::Get();
  const std::string reading =
      list.Add("Reading", std::string(list.UnusedIcon()),
               browser()->GetProfile()->GetBaseName());
  EXPECT_FALSE(Dimmed(item));
  for (int i = 0; i < tabs->count(); ++i) {
    if (!tabs->GetTabAtIndex(i)->GetGroup()) {
      tabs->ActivateTabAt(i);
      break;
    }
  }
  EXPECT_TRUE(Dimmed(item));
  tabs->ActivateTabAt(tabs->GetIndexOfWebContents(page));

  [item.submenu.delegate menuNeedsUpdate:item.submenu];
  ASSERT_EQ(item.submenu.numberOfItems, 1);
  NSMenuItem* row = [item.submenu itemAtIndex:0];
  EXPECT_TRUE([row.title containsString:@"Reading"]);
  [NSApp sendAction:row.action to:row.target from:row];
  ASSERT_TRUE(base::test::RunUntil([&] {
    for (WorkspaceWindow* partner :
         WorkspaceWindow::From(browser())->Partners()) {
      if (partner->workspace() == reading &&
          partner->window()->GetTabStripModel()->GetIndexOfWebContents(page) !=
              TabStripModel::kNoTab) {
        return tabs->GetIndexOfWebContents(page) == TabStripModel::kNoTab;
      }
    }
    return false;
  }));
}

}  // namespace
}  // namespace uw
