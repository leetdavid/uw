// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/browser/ui/look/window_buttons.h"

// The close, minimize, and zoom buttons are macOS's. Windows keeps Chromium's
// caption buttons; see docs/plans/windows.md.

namespace uw {

void PaintWindowButtonDots(gfx::Canvas* canvas,
                           const views::View* view,
                           SkColor color) {}

void SetWindowButtonsHidden(views::Widget* widget, bool hidden) {}

void PressWindowButton(views::Widget* widget,
                       WindowButton button,
                       bool option) {}

}  // namespace uw
