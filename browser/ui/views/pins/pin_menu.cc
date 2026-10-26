// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/browser/ui/views/pins/pin_menu.h"

#include <optional>
#include <string>

#include "base/no_destructor.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/grit/generated_resources.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/menus/simple_menu_model.h"
#include "ui/views/view_tracker.h"
#include "uw/browser/pins/pin_sync.h"
#include "uw/browser/ui/views/pins/home_url_card.h"

namespace uw {
namespace {

// The tile of the pin whose menu is open. Only one menu is open at a time.
views::ViewTracker& MenuTile() {
  static base::NoDestructor<views::ViewTracker> tile;
  return *tile;
}

}  // namespace

bool IsPinCommand(int command) {
  return command >= kResetToHome && command <= kEditHome;
}

void AddPinCommands(ui::SimpleMenuModel* menu, views::View* tile) {
  if (const std::optional<size_t> close =
          menu->GetIndexOfCommandId(TabStripModel::CommandCloseTab)) {
    menu->RemoveItemAt(*close);
  }
  if (const std::optional<size_t> unpin =
          menu->GetIndexOfCommandId(TabStripModel::CommandTogglePinned)) {
    const std::u16string label = menu->GetLabelAt(*unpin);
    menu->RemoveItemAt(*unpin);
    menu->AddSeparator(ui::NORMAL_SEPARATOR);
    menu->AddItem(TabStripModel::CommandTogglePinned, label);
  }
  size_t at = 0;
  for (const auto [command, string] :
       {std::pair(kResetToHome, IDS_UW_RESET_TO_HOME_MAC),
        std::pair(kReplaceHome, IDS_UW_REPLACE_HOME_URL_MAC),
        std::pair(kEditHome, IDS_UW_EDIT_HOME_URL_MAC)}) {
    menu->InsertItemAt(at++, command, l10n_util::GetStringUTF16(string));
  }
  menu->InsertSeparatorAt(at, ui::NORMAL_SEPARATOR);
  MenuTile().SetView(tile);
}

bool CanExecutePinCommand(const tabs::TabInterface* tab) {
  return PinSync::PinFor(tab);
}

void ExecutePinCommand(const tabs::TabInterface* tab, int command) {
  switch (command) {
    case kResetToHome:
      PinSync::ResetToHome(tab);
      break;
    case kReplaceHome:
      PinSync::ReplaceHomeWithCurrent(tab);
      break;
    case kEditHome:
      HomeUrlCard::Show(tab, MenuTile().view());
      break;
  }
}

}  // namespace uw
