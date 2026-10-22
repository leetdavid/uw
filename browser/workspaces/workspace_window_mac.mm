// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/browser/workspaces/workspace_window.h"

#import <AppKit/AppKit.h>

#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "ui/base/base_window.h"
#include "ui/gfx/native_ui_types.h"

namespace uw {
namespace {

NSWindow* NSWindowOf(BrowserWindowInterface* window) {
  return window->GetWindow()->GetNativeWindow().GetNativeNSWindow();
}

}  // namespace

void WorkspaceWindow::SetHidden(bool hidden) {
  hidden_ = hidden;
  SetContentsHidden(hidden);
}

void WorkspaceWindow::SetContentsHidden(bool hidden) {
  NSWindowOf(window_).contentView.hidden = hidden;
}

void WorkspaceWindow::OrderBelow(WorkspaceWindow* above) {
  [NSWindowOf(window_) orderWindow:NSWindowBelow
                        relativeTo:NSWindowOf(above->window_).windowNumber];
}

}  // namespace uw
