// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#ifndef UW_BROWSER_WORKSPACES_WORKSPACE_THEME_H_
#define UW_BROWSER_WORKSPACES_WORKSPACE_THEME_H_

#include <optional>
#include <string_view>

#include "third_party/skia/include/core/SkColor.h"
#include "uw/components/look/theme.h"

class BrowserWindowInterface;
class Profile;

namespace content {
class WebContents;
}

namespace ui {
struct ColorProviderKey;
}

namespace views {
class Widget;
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
// workspace has: the page mode's, with mixers that keep the theme's colors in
// the sidebar, per docs/plans/themes.md#the-theme-apart-from-chromium.
// Chromium's BrowserWidget calls it. A theme from Chromium's theme store still
// replaces uw's look.
void ApplyThemeOfWindow(BrowserWindowInterface* window,
                        ui::ColorProviderKey& key);

// Shows `widget`, a menu or card that opens from the sidebar of `window`,
// such as the theme picker, in the theme's light or dark, while page mode
// gives the rest of the window another.
void ShowInThemeMode(views::Widget* widget, BrowserWindowInterface* window);

// The same for the views menu just opened from the sidebar of `window`.
void ShowOpenMenuInThemeMode(BrowserWindowInterface* window);

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

// The theme of workspace `id` of `profile`: its window's when one shows it,
// else the one it keeps, else the profile's.
Theme ThemeOfWorkspace(std::string_view id, Profile* profile);

// Gives workspace `id` `theme`, through its window when one shows it.
void SetThemeOfWorkspace(std::string_view id, const Theme& theme);

}  // namespace uw

#endif  // UW_BROWSER_WORKSPACES_WORKSPACE_THEME_H_
