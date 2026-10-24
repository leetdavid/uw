// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#ifndef UW_BROWSER_UI_LOOK_MENU_CARD_H_
#define UW_BROWSER_UI_LOOK_MENU_CARD_H_

#include "chrome/browser/ui/color/chrome_color_id.h"
#include "ui/color/color_id.h"

namespace uw {

// The colors of uw's menu cards, such as the Organize menu. They follow light
// and dark mode, not the theme. uw's own color ids after Chromium's: the page
// card's edge takes the first and page_color.h the next few, so these start
// further on.
inline constexpr ui::ColorId kColorMenuCardBackground = kChromeColorsEnd + 16;
inline constexpr ui::ColorId kColorMenuCardHover = kChromeColorsEnd + 17;
inline constexpr ui::ColorId kColorMenuCardText = kChromeColorsEnd + 18;
inline constexpr ui::ColorId kColorMenuCardIcon = kChromeColorsEnd + 19;
inline constexpr ui::ColorId kColorMenuCardMuted = kChromeColorsEnd + 20;
inline constexpr ui::ColorId kColorMenuCardDisabled = kChromeColorsEnd + 21;
inline constexpr ui::ColorId kColorMenuCardLine = kChromeColorsEnd + 22;
inline constexpr ui::ColorId kColorMenuCardChipHover = kChromeColorsEnd + 23;
inline constexpr ui::ColorId kColorMenuCardAccent = kChromeColorsEnd + 24;
// The card's color at 85%, over frosted glass while the window's glass is on.
inline constexpr ui::ColorId kColorMenuCardGlass = kChromeColorsEnd + 25;
// Chromium's menu color at 85%, for its menus that uw frosts.
inline constexpr ui::ColorId kColorMenuGlass = kChromeColorsEnd + 26;

inline constexpr int kMenuCardWidth = 264;
inline constexpr int kMenuCardRadius = 14;
inline constexpr int kMenuCardPadding = 6;
inline constexpr int kMenuRowHeight = 30;
inline constexpr int kMenuRowRadius = 8;

}  // namespace uw

#endif  // UW_BROWSER_UI_LOOK_MENU_CARD_H_
