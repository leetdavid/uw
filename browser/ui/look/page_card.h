// Copyright 2026 The uw Authors. All rights reserved.

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

// On Windows, minimize, maximize, and close are toolbar buttons at the
// toolbar's end, after Chromium's toolbar divider, kWindowButtonsEnd in from
// the card's edge.
inline constexpr int kWindowButtonWidth = 32;
inline constexpr int kWindowButtonHeight = 28;
inline constexpr int kWindowButtonGap = 2;
inline constexpr int kWindowButtonRadius = 8;
inline constexpr int kWindowButtonsDividerWidth = 2;
inline constexpr int kWindowButtonsDividerGap = 6;
inline constexpr int kWindowButtonsWidth =
    kWindowButtonsDividerWidth + kWindowButtonsDividerGap +
    3 * kWindowButtonWidth + 2 * kWindowButtonGap;
inline constexpr int kWindowButtonsEnd = 8;

// The thin line around the card, transparent where there is none. uw's own
// color id, just past Chromium's, so Chromium's color list stays unpatched.
inline constexpr ui::ColorId kColorPageCardEdge = kChromeColorsEnd;

}  // namespace uw

#endif  // UW_BROWSER_UI_LOOK_PAGE_CARD_H_
