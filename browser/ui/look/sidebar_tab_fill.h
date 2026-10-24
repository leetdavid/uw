// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#ifndef UW_BROWSER_UI_LOOK_SIDEBAR_TAB_FILL_H_
#define UW_BROWSER_UI_LOOK_SIDEBAR_TAB_FILL_H_

#include <optional>

#include "chrome/browser/ui/tabs/tab_style.h"
#include "third_party/skia/include/core/SkColor.h"

class SkPath;

namespace gfx {
class Canvas;
class Rect;
}  // namespace gfx

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

// Fills a pinned tab's tile, or the active tab, as glass: white that fades
// down the tile, a lit top edge, and a faint ring. The active tab is white
// instead. The more `glass`, from 1 to 100, the less white and the clearer the
// glass. `shape` is the tab's outline and `bounds` its box, both in pixels at
// `scale`.
void PaintGlassTabFill(gfx::Canvas* canvas,
                       const views::View* tab,
                       const SkPath& shape,
                       const gfx::Rect& bounds,
                       float scale,
                       bool active,
                       double hover,
                       int glass);

// Draws glass's lit top edge and faint ring along `shape`, inside `bounds`,
// both in pixels at `scale`. `bright` lights the edge fully, as on the active
// tab.
void PaintGlassEdge(gfx::Canvas* canvas,
                    const views::View* view,
                    const SkPath& shape,
                    const gfx::Rect& bounds,
                    float scale,
                    bool bright = false);

}  // namespace uw

#endif  // UW_BROWSER_UI_LOOK_SIDEBAR_TAB_FILL_H_
