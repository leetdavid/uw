// Copyright 2026 The uw Authors. All rights reserved.

#ifndef UW_BROWSER_UI_LOOK_PAGE_COLOR_H_
#define UW_BROWSER_UI_LOOK_PAGE_COLOR_H_

#include <optional>

#include "chrome/browser/ui/color/chrome_color_id.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/color/color_id.h"
#include "ui/color/color_provider_key.h"

namespace ui {
class ColorMixer;
}

namespace views {
class View;
}

namespace uw {

// The page color is the color a page paints behind its content. The toolbar
// and the bookmarks bar above the page take it.

// The address field's text and the field behind it while it has focus or holds
// typed text. Chromium gives the field the dropdown's colors then, but the
// field stays on the page color. uw's own color ids, after kColorPageCardEdge.
inline constexpr ui::ColorId kColorPageFieldText = kChromeColorsEnd + 1;
inline constexpr ui::ColorId kColorPageFieldTextDimmed = kChromeColorsEnd + 2;
inline constexpr ui::ColorId kColorPageFieldFocused = kChromeColorsEnd + 3;
inline constexpr ui::ColorId kColorPageFieldFocusedHovered =
    kChromeColorsEnd + 4;

// Sets the page color the window of `browser_view` shows, or none for today's
// toolbar. The window takes it the next time it asks for its colors.
void SetPageColor(const views::View* browser_view,
                  std::optional<SkColor> color);

// The colors the toolbar of `browser_view`'s window takes from its page color,
// or null.
ui::ColorProviderKey::InitializerSupplier* PageColorMixers(
    const views::View* browser_view);

// Chromium's own colors for the field, used while no page color is set.
// Gives the toolbar, the address field, and the bookmarks bar the colors of
// `page`, a page color, unless it is the toolbar's own color under `key`.
void AddPageColorMixers(ui::ColorProvider* provider,
                        const ui::ColorProviderKey& key,
                        SkColor page);

// The page color whose mixers `mixers` are, from PageColorMixers().
std::optional<SkColor> PageColorOfMixers(
    const ui::ColorProviderKey::InitializerSupplier* mixers);

void AddPageFieldDefaults(ui::ColorMixer& mixer);

}  // namespace uw

#endif  // UW_BROWSER_UI_LOOK_PAGE_COLOR_H_
