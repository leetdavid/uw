// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/browser/ui/views/tab_organization/second_click.h"

#import <AppKit/AppKit.h>

namespace uw {

// Only the event macOS is handing out says so: the window asks where the
// click goes before any view sees it.
bool IsSecondClick(const ui::MouseEvent*) {
  NSEvent* event = NSApp.currentEvent;
  return (event.type == NSEventTypeLeftMouseDown ||
          event.type == NSEventTypeLeftMouseUp) &&
         event.clickCount == 2;
}

}  // namespace uw
