// Copyright 2026 The uw Authors. All rights reserved.

#import "uw/browser/ui/views/workspaces/move_task_menu_mac.h"

#import <AppKit/AppKit.h>

#include <memory>
#include <optional>
#include <string>

#include "base/functional/bind.h"
#include "base/memory/weak_ptr.h"
#include "base/strings/sys_string_conversions.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/global_browser_collection.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "components/tab_groups/tab_group_id.h"
#include "components/tabs/public/tab_interface.h"
#include "ui/base/models/menu_model.h"
#include "uw/browser/tab_organization/commands.h"
#include "uw/browser/tab_organization/tab_organization_controller.h"
#include "uw/browser/ui/views/tab_organization/window_commands.h"
#include "uw/browser/workspaces/workspace_move_menu.h"

namespace {

void MoveTask(base::WeakPtr<BrowserWindowInterface> window,
              const tab_groups::TabGroupId& group,
              const std::string& workspace) {
  if (window) {
    uw::TabOrganizationController::MoveTaskToWorkspace(window.get(), group,
                                                       workspace);
  }
}

BrowserWindowInterface* FrontWindow() {
  return GlobalBrowserCollection::GetInstance()->GetLastActiveBrowser();
}

}  // namespace

@interface UwMoveTaskMenu : NSObject <NSMenuDelegate, NSMenuItemValidation>
@end

@implementation UwMoveTaskMenu {
  std::unique_ptr<uw::WorkspaceMoveMenu> _workspaces;
}

- (BOOL)validateMenuItem:(NSMenuItem*)item {
  BrowserWindowInterface* window = FrontWindow();
  return window && uw::CanRunWindowCommand(window, uw::kMoveTaskToWorkspace);
}

// The row opens its submenu. This only lets it be dimmed.
- (void)showWorkspaces:(id)sender {
}

- (void)menuNeedsUpdate:(NSMenu*)menu {
  [menu removeAllItems];
  _workspaces.reset();
  BrowserWindowInterface* window = FrontWindow();
  const tabs::TabInterface* tab =
      window ? window->GetTabStripModel()->GetActiveTab() : nullptr;
  const std::optional<tab_groups::TabGroupId> group =
      tab ? tab->GetGroup() : std::nullopt;
  if (!group) {
    return;
  }
  _workspaces = std::make_unique<uw::WorkspaceMoveMenu>(
      window, base::BindRepeating(&MoveTask, window->GetWeakPtr(), *group));
  for (size_t i = 0; i < _workspaces->GetItemCount(); ++i) {
    NSString* title = base::SysUTF16ToNSString(_workspaces->GetLabelAt(i));
    switch (_workspaces->GetTypeAt(i)) {
      case ui::MenuModel::TYPE_SEPARATOR:
        [menu addItem:NSMenuItem.separatorItem];
        break;
      case ui::MenuModel::TYPE_TITLE:
        if (@available(macOS 14, *)) {
          [menu addItem:[NSMenuItem sectionHeaderWithTitle:title]];
        } else {
          [menu addItemWithTitle:title action:nil keyEquivalent:@""].enabled =
              NO;
        }
        break;
      default: {
        NSMenuItem* row = [[NSMenuItem alloc] initWithTitle:title
                                                     action:@selector(move:)
                                              keyEquivalent:@""];
        row.target = self;
        row.tag = i;
        [menu addItem:row];
      }
    }
  }
}

- (void)move:(NSMenuItem*)sender {
  if (_workspaces) {
    _workspaces->ActivatedAt(sender.tag);
  }
}

@end

namespace uw {

void SetUpMoveTaskMenu(NSMenuItem* item) {
  static UwMoveTaskMenu* __strong menu = [[UwMoveTaskMenu alloc] init];
  item.target = menu;
  item.action = @selector(showWorkspaces:);
  item.submenu.delegate = menu;
}

}  // namespace uw
