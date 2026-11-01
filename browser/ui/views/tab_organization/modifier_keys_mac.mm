// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/browser/ui/views/tab_organization/modifier_keys.h"

#import <AppKit/AppKit.h>

namespace uw {

namespace {

bool HasOption(NSEventModifierFlags flags) {
  return flags & NSEventModifierFlagOption;
}

bool HasCommand(NSEventModifierFlags flags) {
  return flags & NSEventModifierFlagCommand;
}

}  // namespace

ModifierKeys::ModifierKeys() {
  ModifierKeys* self = this;
  [NSEvent
      addLocalMonitorForEventsMatchingMask:NSEventMaskFlagsChanged
                                   handler:^NSEvent*(NSEvent* event) {
                                     self->Set(HasOption(event.modifierFlags),
                                               HasCommand(event.modifierFlags));
                                     return event;
                                   }];
  NSNotificationCenter* center = NSNotificationCenter.defaultCenter;
  [center addObserverForName:NSApplicationDidResignActiveNotification
                      object:nil
                       queue:nil
                  usingBlock:^(NSNotification*) {
                    self->Set(false, false);
                  }];
  [center addObserverForName:NSApplicationDidBecomeActiveNotification
                      object:nil
                       queue:nil
                  usingBlock:^(NSNotification*) {
                    self->Set(HasOption(NSEvent.modifierFlags),
                              HasCommand(NSEvent.modifierFlags));
                  }];
  if (NSApp.active) {
    Set(HasOption(NSEvent.modifierFlags), HasCommand(NSEvent.modifierFlags));
  }
}

}  // namespace uw
