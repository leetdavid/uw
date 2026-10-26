// Copyright 2026 The uw Authors. All rights reserved.

#ifndef UW_BROWSER_UI_LOOK_TINT_COLOR_MIXER_H_
#define UW_BROWSER_UI_LOOK_TINT_COLOR_MIXER_H_

namespace ui {
class ColorProvider;
struct ColorProviderKey;
}  // namespace ui

namespace uw {

// Sets the window frame colors to the tint. Add it before Chromium's own
// mixers so the colors they derive from the frame follow the tint.
void AddTintColorMixer(ui::ColorProvider* provider,
                       const ui::ColorProviderKey& key);

// Sets the tab strip colors that Chromium does not derive from the frame:
// text on the tint and the active tab. Add it after the tab strip mixers.
void AddTintTabColorMixer(ui::ColorProvider* provider,
                          const ui::ColorProviderKey& key);

}  // namespace uw

#endif  // UW_BROWSER_UI_LOOK_TINT_COLOR_MIXER_H_
