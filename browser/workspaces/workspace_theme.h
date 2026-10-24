// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#ifndef UW_BROWSER_WORKSPACES_WORKSPACE_THEME_H_
#define UW_BROWSER_WORKSPACES_WORKSPACE_THEME_H_

#include <optional>

#include "third_party/skia/include/core/SkColor.h"
#include "uw/components/look/theme.h"

class BrowserWindowInterface;

namespace content {
class WebContents;
}

namespace ui {
struct ColorProviderKey;
}

namespace uw {

// The theme a window shows: its workspace's, per docs/plans/workspaces.md, or
// its profile's for a window without a workspace, such as a private one.
Theme ThemeOfWindow(BrowserWindowInterface* window);

// Gives the workspace that `window` shows `theme`, or its profile when it has
// no workspace, and redraws the windows that show it. Chromium's own color
// and mode settings for the profile follow, so its color panel and the
// profile's private windows show the latest pick.
void SetThemeOfWindow(BrowserWindowInterface* window, const Theme& theme);

// The theme a new workspace starts on: the first suggested theme no
// workspace uses, in the mode and page mode of `like`.
Theme NewWorkspaceTheme(const Theme& like);

// Sets the color and the mode in the color key of `window` from the theme its
// workspace has. Chromium's BrowserWidget calls it. A theme from Chromium's
// theme store still replaces uw's look.
void ApplyThemeOfWindow(BrowserWindowInterface* window,
                        ui::ColorProviderKey& key);

// Whether `page` is told to look dark apart from its window, by the page mode
// of the window it is a tab of, per docs/plans/themes.md#page-mode. Nothing
// while it follows the window: under a page mode of Window, for Chromium's own
// pages, outside a tab, and in a private window, whose pages follow macOS.
// Chromium asks it for each page's prefers-color-scheme.
std::optional<bool> PageModeIsDark(content::WebContents* page);

// The page card's color behind `page` before it paints, while page mode tells
// it Light or Dark: white or #0A0A0A.
std::optional<SkColor> EmptyPageColor(content::WebContents* page);

// The profile's theme changed. What changed somewhere other than uw's picker,
// such as a color picked in the Customize Chromium panel or the mode in
// Chromium's settings, becomes part of the theme of the workspace that
// `window`, the profile's front window, shows.
void FollowChromiumTheme(BrowserWindowInterface* window);

// Called once a workspace window starts. A workspace without a theme of its
// own takes its profile's, and uw notes the profile's theme, so that
// FollowChromiumTheme tells uw's own changes from Chromium's.
void StartWindowTheme(BrowserWindowInterface* window);

}  // namespace uw

#endif  // UW_BROWSER_WORKSPACES_WORKSPACE_THEME_H_
