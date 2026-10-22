// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#ifndef UW_BROWSER_UI_LOOK_TINT_PAINTER_H_
#define UW_BROWSER_UI_LOOK_TINT_PAINTER_H_

#include "third_party/skia/include/core/SkColor.h"

class PrefService;

namespace gfx {
class Canvas;
}  // namespace gfx

namespace views {
class View;
}

namespace uw {

// Fills `view` with its part of the window tint. The gradient starts at
// `color`, runs through the more colors of the theme saved in `prefs`, and
// spans `window`, so every view that calls this draws one continuous sheet.
void PaintTint(gfx::Canvas* canvas,
               const views::View* view,
               const views::View* window,
               SkColor color,
               const PrefService& prefs);

// While the sidebar of `window` slides to another workspace, its tint fades
// toward the one `color` and `prefs` give, by `amount` from 0 to 1. Zero
// ends it. `prefs` must last as long as the blend.
void SetTintBlend(const views::View* window,
                  SkColor color,
                  const PrefService* prefs,
                  float amount);

// Draws the edge of the page card, which holds `toolbar` and `page`: a 1px
// line just outside it, where it crosses `view`. Draws nothing in fullscreen
// or when the edge color is transparent.
void PaintPageCardEdge(gfx::Canvas* canvas,
                       const views::View* view,
                       const views::View* toolbar,
                       const views::View* page);

}  // namespace uw

#endif  // UW_BROWSER_UI_LOOK_TINT_PAINTER_H_
