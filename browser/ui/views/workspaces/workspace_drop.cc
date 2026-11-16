// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/browser/ui/views/workspaces/workspace_drop.h"

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "base/callback_list.h"
#include "base/functional/bind.h"
#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/no_destructor.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/tabs/dragging/drag_session_data.h"
#include "chrome/browser/ui/views/tabs/dragging/tab_drag_target.h"
#include "components/tab_groups/tab_group_id.h"
#include "components/tabs/public/tab_interface.h"
#include "ui/gfx/geometry/point.h"
#include "ui/views/view.h"
#include "ui/views/widget/widget.h"
#include "uw/browser/tab_organization/tab_organization_controller.h"
#include "uw/browser/ui/views/workspaces/workspace_switcher.h"

namespace uw {
namespace {

// The icon a drag is over: its switcher and workspace, and the tab strip the
// drag stays attached to meanwhile.
struct HoveredIcon {
  base::WeakPtr<WorkspaceSwitcher> switcher;
  std::string workspace;
  raw_ptr<TabDragContext> context = nullptr;
};

HoveredIcon& Over() {
  static base::NoDestructor<HoveredIcon> icon;
  return *icon;
}

void Leave() {
  if (WorkspaceSwitcher* switcher = Over().switcher.get()) {
    switcher->SetDropTarget(std::string());
  }
  Over() = HoveredIcon();
}

class WorkspaceDropTarget : public TabDragTarget {
 public:
  // TabDragTarget:
  void OnTabDragEntered() override {}
  TabDragContext* OnTabDragUpdated(DragController& controller,
                                   const gfx::Point& point_in_screen) override {
    if (WorkspaceSwitcher* switcher = Over().switcher.get()) {
      switcher->SetDropTarget(Over().workspace);
    }
    return Over().context;
  }
  void OnTabDragExited(const gfx::Point& point_in_screen) override { Leave(); }
  void OnTabDragEnded() override { Leave(); }
  bool CanDropTab() override { return true; }
  void HandleTabDrop(DragController& controller) override {}
  base::CallbackListSubscription RegisterWillDestroyCallback(
      base::OnceClosure callback) override {
    return will_destroy_.Add(std::move(callback));
  }

 private:
  base::OnceClosureList will_destroy_;
};

void MoveTask(base::WeakPtr<BrowserWindowInterface> window,
              const tab_groups::TabGroupId& group,
              const std::string& workspace) {
  if (window) {
    TabOrganizationController::MoveTaskToWorkspace(window.get(), group,
                                                   workspace);
  }
}

void MoveTabs(base::WeakPtr<BrowserWindowInterface> window,
              const std::vector<tabs::TabHandle>& tabs,
              const std::string& workspace) {
  if (window) {
    TabOrganizationController::MoveTabsToWorkspace(window.get(), tabs,
                                                   workspace);
  }
}

}  // namespace

TabDragTarget* WorkspaceDropTargetAt(views::View* region,
                                     TabDragContext* context,
                                     const gfx::Point& point_in_screen) {
  BrowserView* view = region->GetWidget()
                          ? BrowserView::GetBrowserViewForNativeWindow(
                                region->GetWidget()->GetNativeWindow())
                          : nullptr;
  WorkspaceSwitcher* switcher =
      view ? WorkspaceSwitcher::From(view->browser()) : nullptr;
  const std::string workspace =
      switcher ? switcher->WorkspaceAt(point_in_screen) : std::string();
  if (workspace.empty()) {
    return nullptr;
  }
  if (Over().switcher.get() != switcher) {
    Leave();
  }
  Over() = {switcher->GetWeakPtr(), workspace, context};
  static base::NoDestructor<WorkspaceDropTarget> target;
  return target.get();
}

base::OnceClosure TakeWorkspaceDrop(const DragSessionData& data) {
  const HoveredIcon drop = Over();
  WorkspaceSwitcher* switcher = drop.switcher.get();
  if (!switcher || drop.workspace.empty()) {
    return base::OnceClosure();
  }
  BrowserWindowInterface* window = switcher->window();
  if (const std::optional<tab_groups::TabGroupId> group =
          data.group_header_id()) {
    return base::BindOnce(&MoveTask, window->GetWeakPtr(), *group,
                          drop.workspace);
  }
  std::vector<tabs::TabHandle> tabs;
  for (const TabDragData& dragged : data.tab_drag_data_) {
    if (const tabs::TabInterface* tab =
            dragged.contents
                ? tabs::TabInterface::GetFromContents(dragged.contents)
                : nullptr) {
      tabs.push_back(tab->GetHandle());
    }
  }
  return base::BindOnce(&MoveTabs, window->GetWeakPtr(), std::move(tabs),
                        drop.workspace);
}

}  // namespace uw
