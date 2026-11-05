// Copyright 2026 The uw Authors. All rights reserved.

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

namespace uw {

// The theme a window shows: its workspace's, per docs/plans/workspaces.md, or
// its profile's for a window without a workspace, such as a private one.
Theme ThemeOfWindow(BrowserWindowInterface* window);

// Gives the workspace that `window` shows `theme`, or its profile when it has
// no workspace, and redraws the windows that show it. Chromium's own color for
// the profile follows, so its color panel and the profile's private windows
// show the latest pick.
void SetThemeOfWindow(BrowserWindowInterface* window, const Theme& theme);

// The theme a new workspace starts on: the first suggested theme no
// workspace uses, at the brightness of `like`.
Theme NewWorkspaceTheme(const Theme& like);

// Sets the color key of `window`: the sidebar takes the theme it shows, and
// everything else the profile's mode, per docs/plans/themes.md#one-look.
// Chromium's BrowserWidget calls it. A private window keeps Chromium's dark
// look, a web app its own, and a theme from Chromium's theme store still
// replaces uw's look.
void ApplyThemeOfWindow(BrowserWindowInterface* window,
                        ui::ColorProviderKey& key);

// The page card's color behind `page` before it paints, and the toolbar's
// until the page gives its own: white or #0A0A0A by the profile's mode.
std::optional<SkColor> EmptyPageColor(content::WebContents* page);

// The profile's theme changed. What changed somewhere other than uw's picker,
// such as a color picked in the Customize Chromium panel, becomes part of the
// theme of the workspace that `window`, the profile's front window, shows.
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
