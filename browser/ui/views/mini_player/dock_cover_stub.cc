// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/browser/ui/views/mini_player/dock_cover.h"

// Other apps' windows are measured on macOS only so far; see
// docs/plans/windows.md.

namespace uw {

double CoveredPart(views::Widget* window, const gfx::Rect& area) {
  return 0;
}

void SetCoveredPartForTesting(std::optional<double> part) {}

}  // namespace uw
