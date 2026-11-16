// Copyright 2026 The uw Authors. All rights reserved.

#ifndef UW_BROWSER_WORKSPACES_WORKSPACE_MOVE_MENU_H_
#define UW_BROWSER_WORKSPACES_WORKSPACE_MOVE_MENU_H_

#include <string>
#include <vector>

#include "base/functional/callback.h"
#include "ui/menus/simple_menu_model.h"

class BrowserWindowInterface;

namespace uw {

// The workspaces a tab or a task can move to from `window`'s: the profile's
// other workspaces, then, under "Reopens in another profile", the rest.
// Picking one runs `move` with its id. Empty for a window with no workspace.
class WorkspaceMoveMenu : public ui::SimpleMenuModel,
                      public ui::SimpleMenuModel::Delegate {
 public:
  using MoveCallback = base::RepeatingCallback<void(const std::string&)>;

  // A workspace something can move to. `reopens` is true for one on another
  // profile, where pages reopen from their addresses.
  struct Target {
    std::string id;
    std::string icon;
    std::string name;
    bool reopens = false;
  };
  // The menu's workspaces, in its order.
  static std::vector<Target> TargetsFor(BrowserWindowInterface* window);

  WorkspaceMoveMenu(BrowserWindowInterface* window, MoveCallback move);
  WorkspaceMoveMenu(const WorkspaceMoveMenu&) = delete;
  WorkspaceMoveMenu& operator=(const WorkspaceMoveMenu&) = delete;
  ~WorkspaceMoveMenu() override;

  bool empty() const { return ids_.empty(); }

  // ui::SimpleMenuModel::Delegate:
  void ExecuteCommand(int command_id, int event_flags) override;

 private:
  void Add(const Target& target);

  MoveCallback move_;
  std::vector<std::string> ids_;
};

}  // namespace uw

#endif  // UW_BROWSER_WORKSPACES_WORKSPACE_MOVE_MENU_H_
