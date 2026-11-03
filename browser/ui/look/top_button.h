// Copyright 2026 The uw Authors. All rights reserved.

#ifndef UW_BROWSER_UI_LOOK_TOP_BUTTON_H_
#define UW_BROWSER_UI_LOOK_TOP_BUTTON_H_

#include "chrome/browser/ui/color/chrome_color_id.h"
#include "ui/color/color_id.h"
#include "ui/views/controls/button/button.h"

namespace gfx {
class Canvas;
struct VectorIcon;
}  // namespace gfx

namespace uw {

// The look the buttons at the top of the sidebar share: the sidebar toggle,
// Tasks, tab search, and the Inbox. See the Doop guide uw-hidden-sidebar,
// "Top buttons".
inline constexpr int kTopButtonSize = 28;
inline constexpr int kTopButtonRadius = 8;
inline constexpr int kTopButtonIconSize = 16;
// Between Tasks, tab search, and the Inbox.
inline constexpr int kTopButtonSpacing = 2;

// The theme's text color at 75%, at 55% in a window that is not in front,
// and at 100% while a button is lit.
inline constexpr ui::ColorId kColorTopButtonIcon = kChromeColorsEnd + 28;
inline constexpr ui::ColorId kColorTopButtonIconInactive =
    kChromeColorsEnd + 29;
inline constexpr ui::ColorId kColorTopButtonIconLit = kChromeColorsEnd + 30;
// The theme's fill, behind a lit button.
inline constexpr ui::ColorId kColorTopButtonFill = kChromeColorsEnd + 31;

// uw's icon in place of one of Chromium's on these buttons, or `icon` itself.
const gfx::VectorIcon& TopButtonIcon(const gfx::VectorIcon& icon);
// A button is lit under the pointer, and while its menu or pop-up is open,
// which holds it pressed.
bool IsTopButtonLit(views::Button::ButtonState state);
ui::ColorId TopButtonIconColor(bool lit, bool window_in_front);
// Paints the theme's fill that marks a lit button.
void PaintTopButtonFill(gfx::Canvas* canvas, const views::View& button);

}  // namespace uw

#endif  // UW_BROWSER_UI_LOOK_TOP_BUTTON_H_
