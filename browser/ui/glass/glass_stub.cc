// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/browser/ui/glass/glass.h"

// Glass is macOS's Liquid Glass. Other platforms show no glass so far; see
// docs/plans/windows.md.

namespace uw {

bool CanShowGlass() {
  return false;
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
