// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#ifndef UW_BROWSER_UI_LOOK_FAVORITE_TILE_H_
#define UW_BROWSER_UI_LOOK_FAVORITE_TILE_H_

#include <optional>

#include "chrome/browser/ui/tabs/tab_style.h"
#include "third_party/skia/include/core/SkColor.h"

namespace views {
class View;
}

namespace uw {

// The fill of a pinned tab at rest, which is drawn as a favorite tile. Empty
// for other tabs and for a pinned tab that is active or hovered.
std::optional<SkColor> FavoriteTileColor(
    const views::View* tab,
    bool pinned,
    TabStyle::TabSelectionState selection_state,
    bool hovered);

}  // namespace uw

#endif  // UW_BROWSER_UI_LOOK_FAVORITE_TILE_H_
