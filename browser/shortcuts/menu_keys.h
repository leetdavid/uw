// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#ifndef UW_BROWSER_SHORTCUTS_MENU_KEYS_H_
#define UW_BROWSER_SHORTCUTS_MENU_KEYS_H_

#include <string>
#include <string_view>
#include <vector>

class PrefRegistrySimple;
class PrefService;

namespace ui {
class Accelerator;
}

namespace uw {

// The keys the user gave uw's commands of the menu bar, for the whole app: a
// dictionary from a command's name to its key, written as Chromium's key
// recorder writes it, such as "Command+Ctrl+K". An empty key is no key. A
// command that is not listed has its default key. See ADR 0011.
inline constexpr char kMenuKeysPref[] = "uw.menu_keys";

void RegisterMenuKeysPrefs(PrefRegistrySimple* registry);

// One command of the menu bar, with its key written as the recorder writes it.
struct MenuKey {
  // The menu it is in, and the submenu, such as "Edit › Find".
  std::string menu;
  std::string title;
  // What the pref calls it. Empty for Chromium's commands, whose keys cannot
  // be changed.
  std::string name;
  std::string key;
  std::string default_key;
  // The command that holds `default_key` now, when this one lost it.
  std::string taken_by;
};

// Gives the menu bar the keys saved in `local_state`. The menu bar and the
// key lookup then agree: a command's old key stops working, and a Chromium
// command loses a key a uw command took. Call once the menu bar is built.
void ApplyMenuKeys(const PrefService& local_state);

// Every command of the menu bar, menu by menu, in the menu bar's order.
std::vector<MenuKey> MenuKeys();

// Gives the command `name` the key `key`, or no key for an empty one, and
// takes the key from the command that holds it. Changes nothing and returns
// false for a key without Command or one the recorder cannot write.
bool SetMenuKey(PrefService& local_state,
                std::string_view name,
                std::string_view key);

// Gives the command `name` its default key back, taking it from the command
// that holds it.
void ResetMenuKey(PrefService& local_state, std::string_view name);

// Gives `key` back to the Chromium command it belongs to.
void GiveBackMenuKey(PrefService& local_state, std::string_view key);

// Gives every command its default key back.
void ResetMenuKeys(PrefService& local_state);

// Whether `command` has another key than its default, now that the user
// changed keys. If so, and `key` is not null, sets `key` to it, or to an empty
// accelerator when the command has none.
bool MenuKeyChanged(int command, ui::Accelerator* key);

}  // namespace uw

#endif  // UW_BROWSER_SHORTCUTS_MENU_KEYS_H_
