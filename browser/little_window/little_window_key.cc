// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/browser/little_window/little_window_key.h"

#include "base/command_line.h"
#include "base/functional/bind.h"
#include "base/no_destructor.h"
#include "chrome/browser/browser_process.h"
#include "components/prefs/pref_service.h"
#include "uw/browser/little_window/little_window.h"
#include "uw/browser/little_window/little_window_prefs.h"
#include "uw/browser/shortcuts/menu_keys.h"

namespace uw {

namespace {

bool g_listen_in_tests = false;

LittleWindowKeyListener*& Listener() {
  static LittleWindowKeyListener* listener = nullptr;
  return listener;
}

bool Pref(const char* name) {
  return g_browser_process->local_state()->GetBoolean(name);
}

}  // namespace

std::optional<ui::Accelerator> LittleWindowKey() {
  return internal::PlatformLittleWindowKey();
}

void RunNewLittleWindowCommand() {
  if (internal::IsKeyPress() && !Pref(kLittleWindowKeyInUwPref)) {
    return;
  }
  OpenLittleWindow();
}

bool OpenLinkFromAnotherAppInLittleWindow(const GURL& url) {
  if (!Pref(kLittleWindowLinksPref)) {
    return false;
  }
  return OpenLittleWindow(url) != nullptr;
}

// static
void LittleWindowKeyListener::Start() {
  if (!Listener()) {
    Listener() = new LittleWindowKeyListener();
  }
}

// static
LittleWindowKeyListener* LittleWindowKeyListener::Get() {
  return Listener();
}

// static
void LittleWindowKeyListener::ListenInTestsForTesting() {
  g_listen_in_tests = true;
  if (Listener()) {
    Listener()->Update();
  }
}

LittleWindowKeyListener::LittleWindowKeyListener() {
  prefs_.Init(g_browser_process->local_state());
  const auto update = base::BindRepeating(&LittleWindowKeyListener::Update,
                                          base::Unretained(this));
  prefs_.Add(kLittleWindowKeyFromAppsPref, update);
  // A key changed in Keyboard shortcuts moves with the command.
  prefs_.Add(kMenuKeysPref, update);
  Update();
}

LittleWindowKeyListener::~LittleWindowKeyListener() = default;

void LittleWindowKeyListener::Update() {
  ui::GlobalAcceleratorListener* listener =
      ui::GlobalAcceleratorListener::GetInstance();
  if (!listener) {
    return;
  }
  const std::optional<ui::Accelerator> key = LittleWindowKey();
  altgr_ = key ? internal::AltGrCharacter(*key) : std::u16string();
  // Browser tests leave the user's keys in other apps alone.
  const bool in_tests =
      base::CommandLine::ForCurrentProcess()->HasSwitch("test-type");
  std::optional<ui::Accelerator> wanted;
  if (key && Pref(kLittleWindowKeyFromAppsPref) && altgr_.empty() &&
      (!in_tests || g_listen_in_tests)) {
    wanted = key;
  }
  if (wanted == registered_) {
    return;
  }
  if (registered_) {
    listener->UnregisterAccelerator(*registered_, this);
    registered_.reset();
  }
  if (wanted && listener->RegisterAccelerator(*wanted, this)) {
    registered_ = wanted;
  }
}

void LittleWindowKeyListener::OnKeyPressed(const ui::Accelerator& accelerator) {
  // macOS and Windows give uw the key even while uw is in front, where the
  // in-uw switch decides.
  if (internal::IsUwInFront()) {
    if (Pref(kLittleWindowKeyInUwPref)) {
      OpenLittleWindow();
    }
    return;
  }
  internal::BringUwForward();
  OpenLittleWindow();
}

void LittleWindowKeyListener::ExecuteCommand(
    const std::string& accelerator_group_id,
    const std::string& command_id) {}

}  // namespace uw
