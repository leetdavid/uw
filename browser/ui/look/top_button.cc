// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/browser/ui/look/top_button.h"

#include "cc/paint/paint_flags.h"
#include "chrome/app/vector_icons/vector_icons.h"
#include "ui/color/color_provider.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/vector_icon_types.h"
#include "ui/views/vector_icons.h"
#include "ui/views/view.h"
#include "uw/browser/ui/vector_icons/vector_icons.h"

namespace uw {

const gfx::VectorIcon& TopButtonIcon(const gfx::VectorIcon& icon) {
  // Chromium shows "menu open" on a shown sidebar, whose toggle hides it.
  if (&icon == &views::kMenuOpenIcon || &icon == &views::kMenuOpenOldIcon) {
    return kPanelLeftCloseIcon;
  }
  if (&icon == &views::kMenuCloseCustomIcon) {
    return kPanelLeftOpenIcon;
  }
  if (&icon == &kGridViewIcon || &icon == &kSavedTabGroupBarEverythingOldIcon) {
    return kFoldersIcon;
  }
  if (&icon == &kManageSearchIcon || &icon == &kTabSearchTabStripOldIcon) {
    return kTextSearchIcon;
  }
  return icon;
}

bool IsTopButtonLit(views::Button::ButtonState state) {
  return state == views::Button::STATE_HOVERED ||
         state == views::Button::STATE_PRESSED;
}

ui::ColorId TopButtonIconColor(bool lit, bool window_in_front) {
  if (lit) {
    return kColorTopButtonIconLit;
  }
  return window_in_front ? kColorTopButtonIcon : kColorTopButtonIconInactive;
}

void PaintTopButtonFill(gfx::Canvas* canvas, const views::View& button) {
  cc::PaintFlags fill;
  fill.setAntiAlias(true);
  fill.setColor(button.GetColorProvider()->GetColor(kColorTopButtonFill));
  canvas->DrawRoundRect(button.GetLocalBounds(), kTopButtonRadius, fill);
}

}  // namespace uw
