// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/browser/ui/views/workspaces/workspace_swipe.h"

#import <AppKit/AppKit.h>

#include "ui/events/event.h"

namespace uw {

float FingerDelta(const ui::ScrollEvent& event) {
  // Without natural scrolling, macOS turns the fingers' motion around. An
  // event made in a test has no NSEvent, and goes the natural way.
  if (event.HasNativeEvent() &&
      !event.native_event().Get().directionInvertedFromDevice) {
    return -event.x_offset();
  }
  return event.x_offset();
}

}  // namespace uw
