// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/browser/workspaces/workspace_window.h"

// Hiding a workspace's window behind its partner works on the Mac and Windows
// only so far; see docs/plans/windows.md.

namespace uw {

void WorkspaceWindow::SetContentsHidden(bool hidden) {}

void WorkspaceWindow::OrderBelow(WorkspaceWindow* above) {}

}  // namespace uw
