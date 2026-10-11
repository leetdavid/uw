// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/browser/ui/look/favorite_tile.h"

#include "chrome/browser/ui/color/chrome_color_id.h"
#include "ui/color/color_provider.h"
#include "ui/views/view.h"

namespace uw {

std::optional<SkColor> FavoriteTileColor(
    const views::View* tab,
    bool pinned,
    TabStyle::TabSelectionState selection_state,
    bool hovered) {
  if (!pinned || hovered ||
      selection_state != TabStyle::TabSelectionState::kInactive) {
    return std::nullopt;
  }
  return tab->GetColorProvider()->GetColor(
      kColorNewTabButtonCRBackgroundFrameActive);
}

}  // namespace uw
