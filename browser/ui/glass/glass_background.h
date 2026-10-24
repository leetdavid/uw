// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#ifndef UW_BROWSER_UI_GLASS_GLASS_BACKGROUND_H_
#define UW_BROWSER_UI_GLASS_GLASS_BACKGROUND_H_

#include <memory>

#include "base/functional/callback.h"
#include "ui/color/color_id.h"

class BrowserWindowInterface;

namespace gfx {
class Canvas;
class RectF;
}  // namespace gfx

namespace views {
class Background;
class View;
}  // namespace views

namespace uw {

// How glassy the window's elements are now, from 0, flat, to 100.
using GlassOf = base::RepeatingCallback<int()>;

// The glass of `window`'s elements, as ElementGlassOfWindow says.
GlassOf GlassOfWindow(BrowserWindowInterface* window);

// A pill in `color` with corners of `radius`, for buttons on the sidebar. With
// glass on, the pill is lit glass, as pin tiles are.
std::unique_ptr<views::Background> CreateGlassPillBackground(GlassOf glass,
                                                             ui::ColorId color,
                                                             float radius);

// A card in `color` with corners of `radius`, for cards on the sidebar. With
// glass on, the card keeps 85% of its color under a lit top edge and a faint
// ring, so its text still reads.
std::unique_ptr<views::Background> CreateGlassCardBackground(GlassOf glass,
                                                             ui::ColorId color,
                                                             float radius);

// Paints `bounds` of `view` as a lit glass pill with corners of `radius`, at
// `glass` from 1 to 100, brighter by `hover` from 0 to 1.
void PaintGlassPill(gfx::Canvas* canvas,
                    const views::View* view,
                    const gfx::RectF& bounds,
                    float radius,
                    int glass,
                    double hover);

}  // namespace uw

#endif  // UW_BROWSER_UI_GLASS_GLASS_BACKGROUND_H_
