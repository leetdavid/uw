// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/browser/ui/views/workspaces/move_to_workspace_menu.h"

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "base/no_destructor.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/grit/generated_resources.h"
#include "components/tabs/public/tab_interface.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/menus/simple_menu_model.h"
#include "uw/browser/tab_organization/tab_organization_controller.h"
#include "uw/browser/workspaces/workspace_list.h"
#include "uw/browser/workspaces/workspace_window.h"

namespace uw {
namespace {

// The submenu's rows, above the numbers of Chromium's own tab commands.
constexpr int kFirstWorkspace = 56300;

class MoveToWorkspaceMenu : public ui::SimpleMenuModel,
                            public ui::SimpleMenuModel::Delegate {
 public:
  explicit MoveToWorkspaceMenu(tabs::TabInterface* tab)
      : ui::SimpleMenuModel(this), tab_(tab->GetHandle()) {
    const WorkspaceWindow* window =
        WorkspaceWindow::From(tab->GetBrowserWindowInterface());
    const base::FilePath profile =
        tab->GetBrowserWindowInterface()->GetProfile()->GetBaseName();
    std::vector<const WorkspaceList::Workspace*> elsewhere;
    for (const WorkspaceList::Workspace& workspace :
         WorkspaceList::Get().workspaces()) {
      if (workspace.id == window->workspace()) {
        continue;
      }
      if (workspace.profile == profile) {
        Add(workspace);
      } else {
        elsewhere.push_back(&workspace);
      }
    }
    if (elsewhere.empty()) {
      return;
    }
    if (GetItemCount()) {
      AddSeparator(ui::NORMAL_SEPARATOR);
    }
    AddTitle(l10n_util::GetStringUTF16(IDS_UW_REOPENS_IN_ANOTHER_PROFILE));
    for (const WorkspaceList::Workspace* workspace : elsewhere) {
      Add(*workspace);
    }
  }

  bool empty() const { return ids_.empty(); }

  // ui::SimpleMenuModel::Delegate:
  void ExecuteCommand(int command_id, int event_flags) override {
    const size_t index = command_id - kFirstWorkspace;
    if (tabs::TabInterface* tab = tab_.Get(); tab && index < ids_.size()) {
      WorkspaceWindow::MoveTab(tab, ids_[index]);
    }
  }

 private:
  void Add(const WorkspaceList::Workspace& workspace) {
    AddItem(kFirstWorkspace + static_cast<int>(ids_.size()),
            base::UTF8ToUTF16(workspace.icon + "  " + workspace.name));
    ids_.push_back(workspace.id);
  }

  const tabs::TabHandle tab_;
  std::vector<std::string> ids_;
};

// The submenu of the open tab menu. One opens at a time.
std::unique_ptr<MoveToWorkspaceMenu>& OpenMenu() {
  static base::NoDestructor<std::unique_ptr<MoveToWorkspaceMenu>> menu;
  return *menu;
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
  auto submenu = std::make_unique<MoveToWorkspaceMenu>(tab);
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
