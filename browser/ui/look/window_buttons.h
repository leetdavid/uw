// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#ifndef UW_BROWSER_UI_LOOK_WINDOW_BUTTONS_H_
#define UW_BROWSER_UI_LOOK_WINDOW_BUTTONS_H_

#include "third_party/skia/include/core/SkColor.h"

namespace gfx {
class Canvas;
}

namespace views {
class View;
}

namespace uw {

// Paints a dot in `color` under each of the window's close, minimize, and
// zoom buttons that lies in `view`, while the window is inactive. macOS
// draws those buttons in pale gray at a third of their opacity then, which a
// light tint all but hides.
void PaintWindowButtonDots(gfx::Canvas* canvas,
                           const views::View* view,
                           SkColor color);

}  // namespace uw

#endif  // UW_BROWSER_UI_LOOK_WINDOW_BUTTONS_H_
