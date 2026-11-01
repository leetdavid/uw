// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/browser/ui/views/workspaces/workspace_swipe.h"

#include "ui/events/event.h"

namespace uw {

// The swipe follows the fingers on the Mac only so far; see
// docs/plans/windows.md.
float FingerDelta(const ui::ScrollEvent& event) {
  return event.x_offset();
}

}  // namespace uw
