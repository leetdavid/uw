// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/browser/ui/glass/glass.h"

// Glass behind windows is macOS's Liquid Glass. Elsewhere, so far, only the
// window's tiles and buttons are glass, which uw paints itself; see
// docs/plans/windows.md.

namespace uw {

bool CanShowGlass() {
  return false;
}

bool CanShowElementGlass() {
  return true;
}

bool ReducesTransparency() {
  return false;
}

base::CallbackListSubscription OnGlassRulesChanged(
    base::RepeatingClosure callback) {
  return {};
}

void AddFrostedGlass(views::Widget* widget,
                     const gfx::Insets& insets,
                     int corner_radius) {}

void SetFrostedGlass(views::Widget* widget,
                     bool on,
                     const gfx::Insets& insets,
                     int corner_radius,
                     const std::optional<GlassHole>& hole) {}

void FrostPageBubble(views::Widget* widget, content::WebContents* contents) {}

void FrostOpenMenu(BrowserWindowInterface* window) {}

}  // namespace uw
