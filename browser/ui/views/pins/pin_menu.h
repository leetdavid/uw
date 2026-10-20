// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#ifndef UW_BROWSER_UI_VIEWS_PINS_PIN_MENU_H_
#define UW_BROWSER_UI_VIEWS_PINS_PIN_MENU_H_

namespace tabs {
class TabInterface;
}

namespace ui {
class SimpleMenuModel;
}

namespace views {
class View;
}

namespace uw {

// The rows a pinned tab's right-click menu adds. They act on the tab the menu
// is for.
enum PinCommand {
  kResetToHome = 56200,
  kReplaceHome,
  kEditHome,
};

bool IsPinCommand(int command);

// Puts the rows at the top of a pin's `menu`, takes Close Tab out, and moves
// Unpin Tab last. Edit Home URL's card opens under `tile`, the pin's tile.
void AddPinCommands(ui::SimpleMenuModel* menu, views::View* tile);

bool CanExecutePinCommand(const tabs::TabInterface* tab);
void ExecutePinCommand(const tabs::TabInterface* tab, int command);

}  // namespace uw

#endif  // UW_BROWSER_UI_VIEWS_PINS_PIN_MENU_H_
