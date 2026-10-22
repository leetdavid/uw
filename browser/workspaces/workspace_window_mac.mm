// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/browser/workspaces/workspace_window.h"

#import <AppKit/AppKit.h>

#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "ui/base/base_window.h"
#include "ui/gfx/native_ui_types.h"

namespace uw {

void WorkspaceWindow::SetHidden(bool hidden) {
  hidden_ = hidden;
  window_->GetWindow()->GetNativeWindow().GetNativeNSWindow().contentView
      .hidden = hidden;
}

}  // namespace uw
