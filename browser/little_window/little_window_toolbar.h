// Copyright 2026 The uw Authors. All rights reserved.

#ifndef UW_BROWSER_LITTLE_WINDOW_LITTLE_WINDOW_TOOLBAR_H_
#define UW_BROWSER_LITTLE_WINDOW_LITTLE_WINDOW_TOOLBAR_H_

#include <memory>
#include <string>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "base/scoped_observation.h"
#include "ui/menus/simple_menu_model.h"
#include "ui/views/view.h"
#include "ui/views/view_observer.h"
#include "ui/views/view_tracker.h"

class BrowserView;

namespace views {
class MenuRunner;
}

namespace uw {

class LittleWindow;

// Move to Workspace in a little window's toolbar: a pill with the workspace's
// icon and "Move to <name>", and an arrow that opens the menu of workspaces,
// per the canvas guide uw-hidden-sidebar. The toolbar owns the pill; this
// keeps track of it and runs the menu.
class LittleWindowToolbar : public ui::SimpleMenuModel::Delegate,
                            public views::ViewObserver {
 public:
  LittleWindowToolbar(LittleWindow* little_window, BrowserView* browser_view);
  LittleWindowToolbar(const LittleWindowToolbar&) = delete;
  LittleWindowToolbar& operator=(const LittleWindowToolbar&) = delete;
  ~LittleWindowToolbar() override;

  // Opens the menu of workspaces under the arrow.
  void ShowWorkspaceMenu();
  // Shows the little window's workspace on the pill again.
  void Update();

  // ui::SimpleMenuModel::Delegate:
  void ExecuteCommand(int command_id, int event_flags) override;
  bool GetAcceleratorForCommandId(int command_id,
                                  ui::Accelerator* accelerator) const override;

  // views::ViewObserver:
  void OnChildViewAdded(views::View* observed_view,
                        views::View* child) override;

 private:
  // Puts the pill just before the toolbar's menu button, once it has one.
  void Place();

  const raw_ptr<LittleWindow> little_window_;
  const raw_ptr<BrowserView> browser_view_;
  views::ViewTracker pill_;
  views::ViewTracker arrow_;
  std::unique_ptr<ui::SimpleMenuModel> menu_;
  std::unique_ptr<views::MenuRunner> menu_runner_;
  // The workspace each row of the open menu moves the page to.
  std::vector<std::string> rows_;
  base::ScopedObservation<views::View, views::ViewObserver> toolbar_{this};
};

}  // namespace uw

#endif  // UW_BROWSER_LITTLE_WINDOW_LITTLE_WINDOW_TOOLBAR_H_
