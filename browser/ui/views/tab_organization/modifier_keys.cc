// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/browser/ui/views/tab_organization/modifier_keys.h"

#include "base/no_destructor.h"

namespace uw {

// static
ModifierKeys& ModifierKeys::Get() {
  static base::NoDestructor<ModifierKeys> keys;
  return *keys;
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
