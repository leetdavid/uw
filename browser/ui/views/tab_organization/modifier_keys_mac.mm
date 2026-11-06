// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/browser/ui/views/tab_organization/modifier_keys.h"

#import <AppKit/AppKit.h>

namespace uw {

ModifierKeys::ModifierKeys() {
  ModifierKeys* self = this;
  const auto set = [self](NSEventModifierFlags flags) {
    self->Set(
        flags & NSEventModifierFlagOption, flags & NSEventModifierFlagCommand,
        flags & NSEventModifierFlagControl, flags & NSEventModifierFlagShift);
  };
  [NSEvent
      addLocalMonitorForEventsMatchingMask:NSEventMaskFlagsChanged |
                                           NSEventMaskKeyDown
                                   handler:^NSEvent*(NSEvent* event) {
                                     if (event.type == NSEventTypeKeyDown) {
                                       self->Press();
                                     } else {
                                       set(event.modifierFlags);
                                     }
                                     return event;
                                   }];
  NSNotificationCenter* center = NSNotificationCenter.defaultCenter;
  [center addObserverForName:NSApplicationDidResignActiveNotification
                      object:nil
                       queue:nil
                  usingBlock:^(NSNotification*) {
                    set(0);
                  }];
  [center addObserverForName:NSApplicationDidBecomeActiveNotification
                      object:nil
                       queue:nil
                  usingBlock:^(NSNotification*) {
                    set(NSEvent.modifierFlags);
                  }];
  if (NSApp.active) {
    set(NSEvent.modifierFlags);
  }
}

}  // namespace uw
