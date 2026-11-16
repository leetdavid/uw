// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/browser/workspaces/workspace_move_menu.h"

#include <utility>

#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/grit/generated_resources.h"
#include "ui/base/l10n/l10n_util.h"
#include "uw/browser/workspaces/workspace_list.h"
#include "uw/browser/workspaces/workspace_window.h"

namespace uw {
namespace {

// The rows, above the numbers of Chromium's own tab commands.
constexpr int kFirstWorkspace = 56300;

}  // namespace

WorkspaceMoveMenu::WorkspaceMoveMenu(BrowserWindowInterface* window, MoveCallback move)
    : ui::SimpleMenuModel(this), move_(std::move(move)) {
  const WorkspaceWindow* shown = WorkspaceWindow::From(window);
  if (!shown) {
    return;
  }
  const base::FilePath profile = window->GetProfile()->GetBaseName();
  std::vector<const WorkspaceList::Workspace*> elsewhere;
  for (const WorkspaceList::Workspace& workspace :
       WorkspaceList::Get().workspaces()) {
    if (workspace.id == shown->workspace()) {
      continue;
    }
    if (workspace.profile == profile) {
      Add(workspace.id, workspace.icon, workspace.name);
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
    Add(workspace->id, workspace->icon, workspace->name);
  }
}

WorkspaceMoveMenu::~WorkspaceMoveMenu() = default;

void WorkspaceMoveMenu::ExecuteCommand(int command_id, int event_flags) {
  const size_t index = command_id - kFirstWorkspace;
  if (index < ids_.size()) {
    move_.Run(ids_[index]);
  }
}

void WorkspaceMoveMenu::Add(const std::string& id,
                        const std::string& icon,
                        const std::string& name) {
  AddItem(kFirstWorkspace + static_cast<int>(ids_.size()),
          base::UTF8ToUTF16(icon + "  " + name));
  ids_.push_back(id);
}

}  // namespace uw
