// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#ifndef UW_BROWSER_UI_VIEWS_TAB_ORGANIZATION_TINT_STYLE_H_
#define UW_BROWSER_UI_VIEWS_TAB_ORGANIZATION_TINT_STYLE_H_

namespace views {
class Label;
class MdTextButton;
}  // namespace views

namespace uw {

// Makes `button` a filled pill in the colors of the window tint.
void StyleAsPill(views::MdTextButton* button);

// Sets `label` in the tint's text color, left aligned, on no background.
void StyleOnTint(views::Label* label);

}  // namespace uw

#endif  // UW_BROWSER_UI_VIEWS_TAB_ORGANIZATION_TINT_STYLE_H_
