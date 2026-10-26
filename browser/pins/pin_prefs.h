// Copyright 2026 The uw Authors. All rights reserved.

#ifndef UW_BROWSER_PINS_PIN_PREFS_H_
#define UW_BROWSER_PINS_PIN_PREFS_H_

namespace user_prefs {
class PrefRegistrySyncable;
}

namespace uw {

// A profile's pins in sidebar order: a list of {"id", "home"}.
inline constexpr char kPinsPref[] = "uw.pins";

void RegisterPinProfilePrefs(user_prefs::PrefRegistrySyncable* registry);

}  // namespace uw

#endif  // UW_BROWSER_PINS_PIN_PREFS_H_
