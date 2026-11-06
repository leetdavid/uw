// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/browser/little_window/little_window_key.h"

#include "ui/events/event_constants.h"
#include "ui/events/keycodes/keyboard_codes.h"

namespace uw::internal {

std::optional<ui::Accelerator> PlatformLittleWindowKey() {
  return ui::Accelerator(ui::VKEY_N, ui::EF_CONTROL_DOWN | ui::EF_ALT_DOWN);
}

bool IsUwInFront() {
  return false;
}

bool IsKeyPress() {
  return false;
}

std::u16string AltGrCharacter(const ui::Accelerator& key) {
  return std::u16string();
}

void BringUwForward() {}

void JoinActiveSpace(gfx::NativeWindow window) {}

}  // namespace uw::internal
