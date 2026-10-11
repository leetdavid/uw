// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#ifndef UW_BROWSER_UI_VIEWS_TAB_ORGANIZATION_SCRATCHPAD_LABEL_H_
#define UW_BROWSER_UI_VIEWS_TAB_ORGANIZATION_SCRATCHPAD_LABEL_H_

#include <vector>

namespace gfx {
class Canvas;
}

namespace views {
class View;
}

namespace uw {

// Height to leave above `children`, the views of the unpinned tab container
// in order, for the "Scratchpad" label. Zero when the first one is a task,
// which means no tab is ungrouped.
int ScratchpadLabelHeight(const std::vector<views::View*>& children);

// Paints the label on `container` when its layout left room for it.
void PaintScratchpadLabel(const views::View* container,
                          const std::vector<views::View*>& children,
                          gfx::Canvas* canvas);

}  // namespace uw

#endif  // UW_BROWSER_UI_VIEWS_TAB_ORGANIZATION_SCRATCHPAD_LABEL_H_
