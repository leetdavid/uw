// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#ifndef UW_BROWSER_UI_VIEWS_TAB_ORGANIZATION_TINT_STYLE_H_
#define UW_BROWSER_UI_VIEWS_TAB_ORGANIZATION_TINT_STYLE_H_

#include "uw/browser/ui/glass/glass_background.h"

namespace views {
class Label;
class MdTextButton;
class View;
}  // namespace views

namespace uw {

// Makes `button` a filled pill in the colors of the window tint.
void StyleAsPill(views::MdTextButton* button);

// Sets `label` in the tint's text color, left aligned, on no background.
void StyleOnTint(views::Label* label);

// The glass of the window that shows `view`, for views that do not know their
// window. Flat while `view` is in none.
GlassOf GlassOfView(const views::View* view);

}  // namespace uw

#endif  // UW_BROWSER_UI_VIEWS_TAB_ORGANIZATION_TINT_STYLE_H_
