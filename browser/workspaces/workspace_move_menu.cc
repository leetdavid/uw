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

// static
std::vector<WorkspaceMoveMenu::Target> WorkspaceMoveMenu::TargetsFor(
    BrowserWindowInterface* window) {
  std::vector<Target> targets;
  const WorkspaceWindow* shown = WorkspaceWindow::From(window);
  if (!shown) {
    return targets;
  }
  const base::FilePath profile = window->GetProfile()->GetBaseName();
  std::vector<Target> elsewhere;
  for (const WorkspaceList::Workspace& workspace :
       WorkspaceList::Get().workspaces()) {
    if (workspace.id == shown->workspace()) {
      continue;
    }
    const bool reopens = workspace.profile != profile;
    (reopens ? elsewhere : targets)
        .push_back({workspace.id, workspace.icon, workspace.name, reopens});
  }
  targets.insert(targets.end(), elsewhere.begin(), elsewhere.end());
  return targets;
}

WorkspaceMoveMenu::WorkspaceMoveMenu(BrowserWindowInterface* window, MoveCallback move)
    : ui::SimpleMenuModel(this), move_(std::move(move)) {
  bool reopening = false;
  for (const Target& target : TargetsFor(window)) {
    if (target.reopens && !reopening) {
      if (GetItemCount()) {
        AddSeparator(ui::NORMAL_SEPARATOR);
      }
      AddTitle(l10n_util::GetStringUTF16(IDS_UW_REOPENS_IN_ANOTHER_PROFILE));
      reopening = true;
    }
    Add(target);
  }
}

WorkspaceMoveMenu::~WorkspaceMoveMenu() = default;

void WorkspaceMoveMenu::ExecuteCommand(int command_id, int event_flags) {
  const size_t index = command_id - kFirstWorkspace;
  if (index < ids_.size()) {
    move_.Run(ids_[index]);
  }
}

void WorkspaceMoveMenu::Add(const Target& target) {
  AddItem(kFirstWorkspace + static_cast<int>(ids_.size()),
          base::UTF8ToUTF16(target.icon + "  " + target.name));
  ids_.push_back(target.id);
}

}  // namespace uw
