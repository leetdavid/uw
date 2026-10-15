// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#ifndef UW_BROWSER_UI_LOOK_PAGE_CARD_H_
#define UW_BROWSER_UI_LOOK_PAGE_CARD_H_

#include "chrome/browser/ui/color/chrome_color_id.h"
#include "ui/color/color_id.h"
#include "ui/gfx/geometry/insets.h"

namespace uw {

// Beside the vertical tab strip, the toolbar and the page sit in one rounded
// card with the window tint showing around it.
inline constexpr int kPageCardRadius = 12;
inline constexpr gfx::Insets kPageCardMargins = gfx::Insets::TLBR(8, 0, 8, 8);

// The thin line around the card, transparent where there is none. uw's own
// color id, just past Chromium's, so Chromium's color list stays unpatched.
inline constexpr ui::ColorId kColorPageCardEdge = kChromeColorsEnd;

}  // namespace uw

#endif  // UW_BROWSER_UI_LOOK_PAGE_CARD_H_
