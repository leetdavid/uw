// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/browser/little_window/little_window_key.h"

#include <windows.h>

#include "ui/events/event_constants.h"
#include "ui/events/keycodes/keyboard_codes.h"

namespace uw::internal {

std::optional<ui::Accelerator> PlatformLittleWindowKey() {
  // uw's keys can't be changed on Windows yet, so this is the accelerator
  // table's.
  return ui::Accelerator(ui::VKEY_N, ui::EF_CONTROL_DOWN | ui::EF_ALT_DOWN);
}

bool IsUwInFront() {
  DWORD process = 0;
  ::GetWindowThreadProcessId(::GetForegroundWindow(), &process);
  return process == ::GetCurrentProcessId();
}

bool IsKeyPress() {
  const std::optional<ui::Accelerator> key = PlatformLittleWindowKey();
  return key && (::GetKeyState(key->key_code()) & 0x8000);
}

std::u16string AltGrCharacter(const ui::Accelerator& key) {
  // Windows sends AltGr as Ctrl+Alt, so a Ctrl+Alt key takes a letter from
  // keyboards that type one with AltGr, such as "ń" on Polish ones.
  if (!(key.modifiers() & ui::EF_CONTROL_DOWN) ||
      !(key.modifiers() & ui::EF_ALT_DOWN)) {
    return std::u16string();
  }
  // The keyboard of the app in front, which the key would be taken from.
  HKL layout = ::GetKeyboardLayout(
      ::GetWindowThreadProcessId(::GetForegroundWindow(), nullptr));
  BYTE state[256] = {};
  state[VK_CONTROL] = state[VK_LCONTROL] = 0x80;
  state[VK_MENU] = state[VK_RMENU] = 0x80;
  if (key.modifiers() & ui::EF_SHIFT_DOWN) {
    state[VK_SHIFT] = 0x80;
  }
  const UINT code = static_cast<UINT>(key.key_code());
  const UINT scan = ::MapVirtualKeyExW(code, MAPVK_VK_TO_VSC, layout);
  wchar_t typed[8] = {};
  // 0x4 leaves the keyboard's dead-key state as it is.
  const int count =
      ::ToUnicodeEx(code, scan, state, typed, std::size(typed), 0x4, layout);
  if (count == 0 || typed[0] < L' ') {
    return std::u16string();
  }
  return std::u16string(reinterpret_cast<const char16_t*>(typed),
                        count < 0 ? 1 : count);
}

void BringUwForward() {}

void JoinActiveSpace(gfx::NativeWindow window) {}

}  // namespace uw::internal
