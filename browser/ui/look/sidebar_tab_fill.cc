// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/browser/ui/look/sidebar_tab_fill.h"

#include "base/numerics/safe_conversions.h"
#include "chrome/browser/ui/color/chrome_color_id.h"
#include "ui/color/color_id.h"
#include "ui/color/color_provider.h"
#include "ui/gfx/color_utils.h"
#include "ui/views/view.h"

namespace uw {

std::optional<SkColor> SidebarTabFill(
    const views::View* tab,
    bool pinned,
    TabStyle::TabSelectionState selection_state,
    double hover) {
  const ui::ColorProvider* colors = tab->GetColorProvider();
  SkColor fill = SK_ColorTRANSPARENT;
  switch (selection_state) {
    case TabStyle::TabSelectionState::kActive:
      return std::nullopt;
    case TabStyle::TabSelectionState::kSelected:
      fill = colors->GetColor(ui::kColorSysStateHeaderSelect);
      break;
    case TabStyle::TabSelectionState::kInactive:
      if (pinned) {
        fill = colors->GetColor(kColorNewTabButtonCRBackgroundFrameActive);
      }
      break;
  }
  const SkColor hovered =
      colors->GetColor(kColorTabBackgroundInactiveHoverFrameActive);
  return color_utils::GetResultingPaintColor(
      SkColorSetA(hovered,
                  base::ClampRound<SkAlpha>(SkColorGetA(hovered) * hover)),
      fill);
}

}  // namespace uw
