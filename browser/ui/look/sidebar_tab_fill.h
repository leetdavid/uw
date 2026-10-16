// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#ifndef UW_BROWSER_UI_LOOK_SIDEBAR_TAB_FILL_H_
#define UW_BROWSER_UI_LOOK_SIDEBAR_TAB_FILL_H_

#include <optional>

#include "chrome/browser/ui/tabs/tab_style.h"
#include "third_party/skia/include/core/SkColor.h"

namespace views {
class View;
}

namespace uw {

// The fill of a tab in the sidebar, which is see-through so the theme shows
// through it. A tab row has none at rest, a pinned tab is a tile in the
// theme's fill, and a selected tab takes Chromium's selection color. The hover
// color comes in over each of them as `hover` goes from 0 to 1. Empty for the
// active tab, which keeps Chromium's color.
std::optional<SkColor> SidebarTabFill(
    const views::View* tab,
    bool pinned,
    TabStyle::TabSelectionState selection_state,
    double hover);

}  // namespace uw

#endif  // UW_BROWSER_UI_LOOK_SIDEBAR_TAB_FILL_H_
