// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#ifndef UW_BROWSER_UI_LOOK_TINT_PAINTER_H_
#define UW_BROWSER_UI_LOOK_TINT_PAINTER_H_

#include "third_party/skia/include/core/SkColor.h"

namespace gfx {
class Canvas;
}

namespace views {
class View;
}

namespace uw {

// Fills `view` with its part of the window tint. The gradient starts at
// `color` and spans `window`, so every view that calls this draws one
// continuous sheet.
void PaintTint(gfx::Canvas* canvas,
               const views::View* view,
               const views::View* window,
               SkColor color);

}  // namespace uw

#endif  // UW_BROWSER_UI_LOOK_TINT_PAINTER_H_
