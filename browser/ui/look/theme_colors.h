// Copyright 2026 The uw Authors. All rights reserved.

#ifndef UW_BROWSER_UI_LOOK_THEME_COLORS_H_
#define UW_BROWSER_UI_LOOK_THEME_COLORS_H_

#include <optional>

#include "third_party/skia/include/core/SkColor.h"
#include "ui/color/color_provider_key.h"

namespace uw {

// The mixers of a window whose mode is not its theme's light or dark, per
// docs/plans/themes.md#one-look: the window's key is in the mode, and these
// give the sidebar and the edge around the page card the colors `theme_key`,
// the key of the window in its theme's own light or dark, gives them. They add
// the colors of `page_color`, the window's page color, when it has one. They
// ride in the key, so the window's colors are kept apart from those of a
// window with another theme.
ui::ColorProviderKey::InitializerSupplier* ThemeColorMixers(
    const ui::ColorProviderKey& theme_key,
    std::optional<SkColor> page_color);

}  // namespace uw

#endif  // UW_BROWSER_UI_LOOK_THEME_COLORS_H_
