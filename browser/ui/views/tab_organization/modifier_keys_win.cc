// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/browser/ui/views/tab_organization/modifier_keys.h"

#include <windows.h>

#include <memory>
#include <utility>

#include "base/functional/bind.h"
#include "base/functional/callback.h"
#include "base/no_destructor.h"
#include "ui/events/event.h"
#include "ui/events/event_observer.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/gfx/native_ui_types.h"
#include "ui/gfx/win/singleton_hwnd.h"
#include "ui/views/event_monitor.h"

namespace uw {

namespace {

// Option is Alt on Windows, and Cmd is Ctrl. Windows sends a lone Alt or Ctrl
// press as a key event, which passes an application monitor whichever window
// has focus. Switching to another app lets go of every key, as on the Mac.
class KeyWatcher : public ui::EventObserver {
 public:
  using SetKeys = base::RepeatingCallback<
      void(bool option, bool command, bool control, bool shift)>;

  KeyWatcher(SetKeys set, base::RepeatingClosure press)
      : set_(std::move(set)),
        press_(std::move(press)),
        monitor_(views::EventMonitor::CreateApplicationMonitor(
            this,
            gfx::NativeWindow(),
            {ui::EventType::kKeyPressed, ui::EventType::kKeyReleased})),
        activation_(gfx::SingletonHwnd::GetInstance()->RegisterCallback(
            base::BindRepeating(&KeyWatcher::OnMessage,
                                base::Unretained(this)))) {}

  // ui::EventObserver:
  void OnEvent(const ui::Event& event) override {
    set_.Run(event.IsAltDown(), event.IsControlDown(), false,
             event.IsShiftDown());
    if (event.type() == ui::EventType::kKeyPressed &&
        !IsModifier(event.AsKeyEvent()->key_code())) {
      press_.Run();
    }
  }

 private:
  static bool IsModifier(ui::KeyboardCode key) {
    switch (key) {
      case ui::VKEY_SHIFT:
      case ui::VKEY_LSHIFT:
      case ui::VKEY_RSHIFT:
      case ui::VKEY_CONTROL:
      case ui::VKEY_LCONTROL:
      case ui::VKEY_RCONTROL:
      case ui::VKEY_MENU:
      case ui::VKEY_LMENU:
      case ui::VKEY_RMENU:
      case ui::VKEY_LWIN:
      case ui::VKEY_RWIN:
        return true;
      default:
        return false;
    }
  }

  void OnMessage(HWND window, UINT message, WPARAM active, LPARAM thread) {
    if (message == WM_ACTIVATEAPP && !active) {
      set_.Run(false, false, false, false);
    }
  }

  const SetKeys set_;
  const base::RepeatingClosure press_;
  const std::unique_ptr<views::EventMonitor> monitor_;
  const base::CallbackListSubscription activation_;
};

}  // namespace

ModifierKeys::ModifierKeys() {
  static base::NoDestructor<KeyWatcher> watcher(
      base::BindRepeating(&ModifierKeys::Set, base::Unretained(this)),
      base::BindRepeating(&ModifierKeys::Press, base::Unretained(this)));
}

}  // namespace uw
