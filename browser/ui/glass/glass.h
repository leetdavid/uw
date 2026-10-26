// Copyright 2026 The uw Authors. All rights reserved.

#ifndef UW_BROWSER_UI_GLASS_GLASS_H_
#define UW_BROWSER_UI_GLASS_GLASS_H_

#include <cstddef>
#include <optional>

#include "base/callback_list.h"
#include "base/functional/callback.h"
#include "third_party/skia/include/core/SkColor.h"

class BrowserWindowInterface;

namespace content {
class WebContents;
}

namespace gfx {
class Insets;
}

namespace views {
class Widget;
}

namespace uw {

// Transparency lets the desktop show through a window under its theme, and
// glass turns the window's tiles and buttons to glass, per the "Glass"
// section of docs/plans/themes.md. Chromium's GlassFrameService puts macOS's
// Liquid Glass behind each window these rules allow.

// True when `window` asks for Liquid Glass behind it: its theme's transparency
// is off solid, macOS can draw glass, and macOS is not reducing transparency.
bool WantsGlass(BrowserWindowInterface* window);

// How many windows show glass at once: every one, or only the front one while
// "Glass in windows behind the front one" is off.
size_t MaxGlassWindows();

// True when this Mac can draw glass at all.
bool CanShowGlass();

// True while macOS's Reduce transparency setting is on.
bool ReducesTransparency();

// Runs `callback` when the rules above may answer differently: the setting for
// windows behind changed, or macOS's Reduce transparency did.
base::CallbackListSubscription OnGlassRulesChanged(
    base::RepeatingClosure callback);

// Whether `window` shows glass now, as `query` from GlassFrameService says.
// Without one, no window does.
bool ShowsGlass(BrowserWindowInterface* window);
void SetGlassQuery(
    base::RepeatingCallback<bool(BrowserWindowInterface*)> query);

// How much of the glass behind `window` its theme covers: all of it unless the
// window shows glass.
float CoverOfWindow(BrowserWindowInterface* window);

// How glassy the tiles and buttons of `window` are, from 0, flat, to 100.
// Flat while macOS reduces transparency.
int ElementGlassOfWindow(BrowserWindowInterface* window);

// How much of the glass behind `window` its toolbar covers with `color`: as
// much as the theme does, but never so little that the toolbar's text stops
// reading over a white or a black desktop.
float ToolbarCoverOfWindow(BrowserWindowInterface* window, SkColor color);

// Puts macOS's heavier, frosted glass behind the card that `widget` shows,
// inside the card's shadow, `insets` in from the widget's edges, with corners
// of `corner_radius`. The card lays its own see-through color over it.
void AddFrostedGlass(views::Widget* widget,
                     const gfx::Insets& insets,
                     int corner_radius);

// A hole near the top of frosted glass, for what shows through it from the
// window below: `top` down from the glass's top edge, `sides` in from its
// left and right edges, `height` tall, with corners of `radius`.
struct GlassHole {
  int top = 0;
  int sides = 0;
  int height = 0;
  int radius = 0;
};

// Puts frosted glass behind `widget` as AddFrostedGlass() does, with `hole`
// cut out of it, while `on`, and takes it away when not. For a widget that
// shows many times.
void SetFrostedGlass(views::Widget* widget,
                     bool on,
                     const gfx::Insets& insets,
                     int corner_radius,
                     const std::optional<GlassHole>& hole);

// Puts frosted glass behind a web page's bubble `widget`, whose page in
// `contents` then draws its own color, see-through while glass is on.
// Nothing on a Mac that cannot draw glass.
void FrostPageBubble(views::Widget* widget, content::WebContents* contents);

// Frosts the menu that has just opened over `window`, and each of its submenus
// as it opens, while the window's tiles and buttons are glass.
void FrostOpenMenu(BrowserWindowInterface* window);

}  // namespace uw

#endif  // UW_BROWSER_UI_GLASS_GLASS_H_
