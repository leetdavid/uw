// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/browser/ui/views/tab_organization/modifier_keys.h"

#import <AppKit/AppKit.h>

#include "base/no_destructor.h"

namespace uw {

namespace {

bool HasOption(NSEventModifierFlags flags) {
  return flags & NSEventModifierFlagOption;
}

bool HasCommand(NSEventModifierFlags flags) {
  return flags & NSEventModifierFlagCommand;
}

}  // namespace

// static
ModifierKeys& ModifierKeys::Get() {
  static base::NoDestructor<ModifierKeys> keys;
  return *keys;
}

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

ModifierKeys::~ModifierKeys() = default;

base::CallbackListSubscription ModifierKeys::Observe(
    base::RepeatingClosure changed) {
  return observers_.Add(std::move(changed));
}

void ModifierKeys::SetForTesting(bool option, bool command) {
  Set(option, command);
}

void ModifierKeys::Set(bool option, bool command) {
  if (option == option_ && command == command_) {
    return;
  }
  option_ = option;
  command_ = command;
  observers_.Notify();
}

}  // namespace uw
