// Copyright 2026 The uw Authors. All rights reserved.

#ifndef UW_BROWSER_UI_VIEWS_WORKSPACES_WORKSPACE_DROP_H_
#define UW_BROWSER_UI_VIEWS_WORKSPACES_WORKSPACE_DROP_H_

#include "base/functional/callback_forward.h"

struct DragSessionData;
class TabDragContext;
class TabDragTarget;

namespace gfx {
class Point;
}

namespace views {
class View;
}

namespace uw {

// Where a tab drag in `region`'s window, attached to `context`, is over the
// icon of another workspace at the sidebar's foot: a target that keeps the
// tabs where they are and gives the icon its hover fill. Null elsewhere.
TabDragTarget* WorkspaceDropTargetAt(views::View* region,
                                     TabDragContext* context,
                                     const gfx::Point& point_in_screen);

// For a drag that ends over such an icon, what moves the dragged task, or
// tabs, to that workspace once the drag has put them back. Null otherwise.
base::OnceClosure TakeWorkspaceDrop(const DragSessionData& data);

}  // namespace uw

#endif  // UW_BROWSER_UI_VIEWS_WORKSPACES_WORKSPACE_DROP_H_
