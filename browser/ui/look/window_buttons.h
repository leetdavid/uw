// Copyright 2026 The uw Authors. All rights reserved.

#ifndef UW_BROWSER_UI_LOOK_WINDOW_BUTTONS_H_
#define UW_BROWSER_UI_LOOK_WINDOW_BUTTONS_H_

#include "third_party/skia/include/core/SkColor.h"

namespace gfx {
class Canvas;
}

namespace views {
class View;
class Widget;
}  // namespace views

namespace uw {

// Paints a dot in `color` under each of the window's close, minimize, and
// zoom buttons that lies in `view`, while the window is inactive. macOS
// draws those buttons in pale gray at a third of their opacity then, which a
// light tint all but hides.
void PaintWindowButtonDots(gfx::Canvas* canvas,
                           const views::View* view,
                           SkColor color);

// Hides or shows macOS's close, minimize, and zoom buttons of `widget`. A
// hidden sidebar takes them away, and its floating card draws uw's own.
void SetWindowButtonsHidden(views::Widget* widget, bool hidden);

enum class WindowButton { kClose, kMinimize, kZoom };

// Does what a click on macOS's `button` of `widget` does: closes, minimizes,
// or goes full screen. With `option`, the zoom button zooms instead.
void PressWindowButton(views::Widget* widget, WindowButton button, bool option);

}  // namespace uw

#endif  // UW_BROWSER_UI_LOOK_WINDOW_BUTTONS_H_
