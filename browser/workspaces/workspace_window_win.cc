// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/browser/workspaces/workspace_window.h"

#include <windows.h>

#include "ui/views/widget/widget.h"
#include "ui/views/win/hwnd_util.h"

namespace uw {

// Windows hides a window's pages with the window.
void WorkspaceWindow::SetContentsHidden(bool hidden) {}

// No see-through theme on Windows shows the window under another.
void WorkspaceWindow::SetSeen(bool seen) {}

void WorkspaceWindow::OrderBelow(WorkspaceWindow* above) {
  views::Widget* widget = GetWidget();
  views::Widget* above_widget = above->GetWidget();
  if (!widget || !above_widget) {
    return;
  }
  const HWND hwnd = views::HWNDForWidget(widget);
  const HWND above_hwnd = views::HWNDForWidget(above_widget);
  constexpr UINT kKeepPlace = SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE;
  // Showing without activating can still put the window on top, as it does a
  // maximized one, so it goes under `above` before and after.
  ::SetWindowPos(hwnd, above_hwnd, 0, 0, 0, 0, kKeepPlace);
  widget->ShowInactive();
  ::SetWindowPos(hwnd, above_hwnd, 0, 0, 0, 0, kKeepPlace);
}

}  // namespace uw
