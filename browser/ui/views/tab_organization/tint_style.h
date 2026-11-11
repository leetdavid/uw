// Copyright 2026 The uw Authors. All rights reserved.

#ifndef UW_BROWSER_UI_VIEWS_TAB_ORGANIZATION_TINT_STYLE_H_
#define UW_BROWSER_UI_VIEWS_TAB_ORGANIZATION_TINT_STYLE_H_

#include "ui/views/view.h"
#include "ui/views/view_utils.h"
#include "uw/browser/ui/glass/glass_background.h"

class BrowserView;

namespace tabs {
class TabInterface;
}  // namespace tabs

namespace views {
class Label;
class MdTextButton;
}  // namespace views

namespace uw {

class TabOrganizationController;

// Makes `button` a filled pill in the colors of the window tint.
void StyleAsPill(views::MdTextButton* button);

// Sets `label` in the tint's text color, left aligned, on no background.
void StyleOnTint(views::Label* label);

// The glass of the window that shows `view`, for views that do not know their
// window. Flat while `view` is in none.
GlassOf GlassOfView(const views::View* view);

// The browser window `view` belongs to, or null while it is in none.
BrowserView* BrowserViewOf(const views::View* view);

// The controller of the window `tab` is in, or null for no tab.
TabOrganizationController* ControllerOf(tabs::TabInterface* tab);

// The first child of `row` that is a T, or null.
template <typename T>
T* ChildOfClass(const views::View* row) {
  for (views::View* child : row->children()) {
    if (T* found = views::AsViewClass<T>(child)) {
      return found;
    }
  }
  return nullptr;
}

}  // namespace uw

#endif  // UW_BROWSER_UI_VIEWS_TAB_ORGANIZATION_TINT_STYLE_H_
