// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/browser/shortcuts/menu_keys.h"

// The keys live in the Mac's menu bar. Other platforms have no menu bar, and
// their keys come later; see docs/plans/windows.md.

namespace uw {

void ApplyMenuKeys(const PrefService& local_state) {}

std::vector<MenuKey> MenuKeys() {
  return {};
}

bool SetMenuKey(PrefService& local_state,
                std::string_view name,
                std::string_view key) {
  return false;
}

void ResetMenuKey(PrefService& local_state, std::string_view name) {}

void GiveBackMenuKey(PrefService& local_state, std::string_view key) {}

void ResetMenuKeys(PrefService& local_state) {}

bool MenuKeyChanged(int command, ui::Accelerator* key) {
  return false;
}

}  // namespace uw
