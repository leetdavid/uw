// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/browser/ui/views/workspaces/move_to_workspace_menu.h"

#include <memory>
#include <optional>
#include <string>

#include "base/functional/bind.h"
#include "base/no_destructor.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/grit/generated_resources.h"
#include "components/tabs/public/tab_interface.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/menus/simple_menu_model.h"
#include "uw/browser/tab_organization/tab_organization_controller.h"
#include "uw/browser/workspaces/workspace_move_menu.h"
#include "uw/browser/workspaces/workspace_window.h"

namespace uw {
namespace {

// The submenu of the open tab menu. One opens at a time.
std::unique_ptr<WorkspaceMoveMenu>& OpenMenu() {
  static base::NoDestructor<std::unique_ptr<WorkspaceMoveMenu>> menu;
  return *menu;
}

void MoveTab(const tabs::TabHandle& handle, const std::string& id) {
  if (tabs::TabInterface* tab = handle.Get()) {
    WorkspaceWindow::MoveTab(tab, id);
  }
}

}  // namespace

bool IsMoveToWorkspaceCommand(int command) {
  return command == kMoveToWorkspaceCommand;
}

void AddMoveToWorkspace(ui::SimpleMenuModel* menu,
                        const tabs::TabInterface* menu_tab) {
  tabs::TabInterface* tab = menu_tab->GetHandle().Get();
  if (!tab || tab->IsPinned() ||
      !WorkspaceWindow::From(tab->GetBrowserWindowInterface())) {
    return;
  }
  auto submenu = std::make_unique<WorkspaceMoveMenu>(
      tab->GetBrowserWindowInterface(),
      base::BindRepeating(&MoveTab, tab->GetHandle()));
  if (submenu->empty()) {
    return;
  }
  std::optional<size_t> after =
      menu->GetIndexOfCommandId(TabOrganizationController::kScratchpad);
  if (!after) {
    after = menu->GetIndexOfCommandId(TabStripModel::CommandAddToExistingGroup);
  }
  menu->InsertSubMenuAt(
      after ? *after + 1 : menu->GetItemCount(), kMoveToWorkspaceCommand,
      l10n_util::GetStringUTF16(IDS_UW_MOVE_TO_WORKSPACE), submenu.get());
  OpenMenu() = std::move(submenu);
}

}  // namespace uw
