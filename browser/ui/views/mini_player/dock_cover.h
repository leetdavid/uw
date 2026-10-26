// Copyright 2026 The uw Authors. All rights reserved.

#ifndef UW_BROWSER_UI_VIEWS_MINI_PLAYER_DOCK_COVER_H_
#define UW_BROWSER_UI_VIEWS_MINI_PLAYER_DOCK_COVER_H_

#include <optional>

namespace gfx {
class Rect;
}

namespace views {
class Widget;
}

namespace uw {

// How much of a docked mini player other apps' windows must cover, with
// another app active, before it floats.
inline constexpr double kHiddenPart = 0.5;

// How much of `area`, in screen coordinates, other apps' ordinary windows
// above `window` cover, from 0 to 1. The menu bar, the Dock, banners, and uw's
// own windows do not count. All of it is covered while macOS shows no part of
// `window`, as on another Space.
double CoveredPart(views::Widget* window, const gfx::Rect& area);

// Makes CoveredPart() answer `part`, for tests, whose windows other apps never
// cover. std::nullopt asks macOS again.
void SetCoveredPartForTesting(std::optional<double> part);

}  // namespace uw

#endif  // UW_BROWSER_UI_VIEWS_MINI_PLAYER_DOCK_COVER_H_
