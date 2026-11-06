// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/browser/little_window/little_window_key.h"

#import <AppKit/AppKit.h>

#include "chrome/browser/ui/cocoa/accelerators_cocoa.h"  // nogncheck
#include "ui/events/keycodes/keyboard_codes.h"
#include "uw/browser/little_window/little_window_commands.h"
#include "uw/browser/shortcuts/menu_keys.h"

namespace uw::internal {

std::optional<ui::Accelerator> PlatformLittleWindowKey() {
  ui::Accelerator changed;
  if (MenuKeyChanged(kNewLittleWindowCommand, &changed)) {
    if (changed.key_code() == ui::VKEY_UNKNOWN) {
      return std::nullopt;
    }
    return changed;
  }
  const ui::Accelerator* key =
      AcceleratorsCocoa::GetInstance()->GetAcceleratorForCommand(
          kNewLittleWindowCommand);
  return key ? std::optional<ui::Accelerator>(*key) : std::nullopt;
}

bool IsUwInFront() {
  return NSApp.isActive;
}

bool IsKeyPress() {
  return NSApp.currentEvent.type == NSEventTypeKeyDown;
}

std::u16string AltGrCharacter(const ui::Accelerator& key) {
  return std::u16string();
}

void BringUwForward() {
  [NSApp activateIgnoringOtherApps:YES];
}

void JoinActiveSpace(gfx::NativeWindow window) {
  NSWindow* ns_window = window.GetNativeNSWindow();
  ns_window.collectionBehavior |=
      NSWindowCollectionBehaviorMoveToActiveSpace |
      NSWindowCollectionBehaviorFullScreenAuxiliary;
}

}  // namespace uw::internal
