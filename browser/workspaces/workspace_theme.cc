// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/browser/workspaces/workspace_theme.h"

#include <algorithm>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "base/auto_reset.h"
#include "base/no_destructor.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/themes/theme_service.h"
#include "chrome/browser/themes/theme_service_factory.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface_iterator.h"
#include "ui/base/base_window.h"
#include "ui/base/mojom/themes.mojom.h"
#include "ui/color/color_provider_key.h"
#include "ui/native_theme/native_theme.h"
#include "ui/views/widget/widget.h"
#include "uw/browser/workspaces/workspace_list.h"
#include "uw/browser/workspaces/workspace_window.h"
#include "uw/components/look/theme_prefs.h"

namespace uw {
namespace {

// Set while uw saves a theme, whose writes to Chromium's settings are its own.
bool g_saving = false;

// The theme of each profile as uw last saw it, by the profile's folder.
std::map<base::FilePath, Theme>& SeenThemes() {
  static base::NoDestructor<std::map<base::FilePath, Theme>> seen;
  return *seen;
}

ThemeMode ModeOf(ThemeService::BrowserColorScheme scheme) {
  switch (scheme) {
    case ThemeService::BrowserColorScheme::kSystem:
      return ThemeMode::kAuto;
    case ThemeService::BrowserColorScheme::kLight:
      return ThemeMode::kLight;
    case ThemeService::BrowserColorScheme::kDark:
      return ThemeMode::kDark;
  }
}

ThemeService::BrowserColorScheme SchemeOf(ThemeMode mode) {
  switch (mode) {
    case ThemeMode::kAuto:
      return ThemeService::BrowserColorScheme::kSystem;
    case ThemeMode::kLight:
      return ThemeService::BrowserColorScheme::kLight;
    case ThemeMode::kDark:
      return ThemeService::BrowserColorScheme::kDark;
  }
}

// The theme Chromium's settings for `profile` hold, with uw's own parts.
Theme ProfileTheme(Profile* profile) {
  const ThemeService* themes = ThemeServiceFactory::GetForProfile(profile);
  Theme theme = ReadTheme(*profile->GetPrefs(), themes->GetUserColor());
  theme.mode = ModeOf(themes->GetBrowserColorScheme());
  return theme;
}

// The workspace that `window` shows, or null.
const WorkspaceList::Workspace* WorkspaceOf(BrowserWindowInterface* window) {
  const WorkspaceWindow* shown = WorkspaceWindow::From(window);
  return shown ? WorkspaceList::Get().Find(shown->workspace()) : nullptr;
}

// Each window that shows `workspace` takes its theme again.
void Redraw(const std::string& workspace) {
  ForEachCurrentBrowserWindowInterfaceOrderedByActivation(
      [&](BrowserWindowInterface* window) {
        const WorkspaceWindow* shown = WorkspaceWindow::From(window);
        if (shown && shown->workspace() == workspace) {
          if (views::Widget* widget = views::Widget::GetWidgetForNativeWindow(
                  window->GetWindow()->GetNativeWindow())) {
            widget->ThemeChanged();
          }
        }
        return true;
      });
}

}  // namespace

Theme ThemeOfWindow(BrowserWindowInterface* window) {
  const WorkspaceList::Workspace* workspace = WorkspaceOf(window);
  return workspace && workspace->theme ? *workspace->theme
                                       : ProfileTheme(window->GetProfile());
}

void SetThemeOfWindow(BrowserWindowInterface* window, const Theme& theme) {
  Profile* profile = window->GetProfile();
  const WorkspaceWindow* shown = WorkspaceWindow::From(window);
  const std::string workspace =
      shown && WorkspaceList::Get().Find(shown->workspace())
          ? shown->workspace()
          : std::string();
  // Before Chromium's settings, whose observers read the window's theme.
  if (!workspace.empty()) {
    WorkspaceList::Get().SetTheme(workspace, theme);
  }
  {
    base::AutoReset<bool> saving(&g_saving, true);
    WriteTheme(*profile->GetPrefs(), theme);
    ThemeService* themes = ThemeServiceFactory::GetForProfile(profile);
    themes->SetUserColorAndBrowserColorVariant(
        WithIntensity(theme.colors[0], theme.intensity),
        ui::mojom::BrowserColorVariant::kTonalSpot);
    themes->SetBrowserColorScheme(SchemeOf(theme.mode));
  }
  SeenThemes()[profile->GetPath()] = ProfileTheme(profile);
  if (!workspace.empty()) {
    Redraw(workspace);
  }
}

Theme NewWorkspaceTheme(const Theme& like) {
  Theme theme;
  theme.mode = like.mode;
  const std::vector<std::vector<SkColor>> suggested = SuggestedThemes();
  theme.colors = suggested.front();
  for (const std::vector<SkColor>& colors : suggested) {
    const bool used = std::ranges::any_of(
        WorkspaceList::Get().workspaces(),
        [&](const WorkspaceList::Workspace& workspace) {
          return workspace.theme && workspace.theme->colors == colors;
        });
    if (!used) {
      theme.colors = colors;
      break;
    }
  }
  return theme;
}

void ApplyThemeOfWindow(BrowserWindowInterface* window,
                        ui::ColorProviderKey& key) {
  const WorkspaceList::Workspace* workspace = WorkspaceOf(window);
  if (!workspace || !workspace->theme || key.custom_theme) {
    return;
  }
  const Theme& theme = *workspace->theme;
  using ColorMode = ui::ColorProviderKey::ColorMode;
  switch (theme.mode) {
    case ThemeMode::kAuto:
      key.color_mode =
          ui::NativeTheme::GetInstanceForNativeUi()->preferred_color_scheme() ==
                  ui::NativeTheme::PreferredColorScheme::kDark
              ? ColorMode::kDark
              : ColorMode::kLight;
      break;
    case ThemeMode::kLight:
      key.color_mode = ColorMode::kLight;
      break;
    case ThemeMode::kDark:
      key.color_mode = ColorMode::kDark;
      break;
  }
  using Source = ui::ColorProviderKey::UserColorSource;
  // Dark mode is black unless "Theme colors in dark mode" is on.
  const bool black =
      key.color_mode == ColorMode::kDark && !theme.colors_in_dark_mode;
  // Until a color is picked, Chromium keeps its own colors, as it does for a
  // profile with none.
  if (theme.colors == Theme().colors && theme.intensity == kFullIntensity) {
    key.user_color = std::nullopt;
    key.scheme_variant = std::nullopt;
    key.user_color_source = black ? Source::kGrayscale : Source::kBaseline;
    return;
  }
  key.user_color = WithIntensity(theme.colors[0], theme.intensity);
  key.scheme_variant = ui::ColorProviderKey::SchemeVariant::kTonalSpot;
  key.user_color_source = black ? Source::kGrayscale : Source::kAccent;
}

void FollowChromiumTheme(BrowserWindowInterface* window) {
  Profile* profile = window->GetProfile();
  // Each window hears of the change; the one in front takes it.
  BrowserWindowInterface* front = nullptr;
  ForEachCurrentBrowserWindowInterfaceOrderedByActivation(
      [&](BrowserWindowInterface* each) {
        if (each->GetProfile() == profile && WorkspaceWindow::From(each) &&
            !WorkspaceWindow::IsHidden(each)) {
          front = each;
          return false;
        }
        return true;
      });
  const auto seen = SeenThemes().find(profile->GetPath());
  if (g_saving || front != window || seen == SeenThemes().end()) {
    return;
  }
  const Theme now = ProfileTheme(profile);
  const Theme before = std::exchange(seen->second, now);
  const WorkspaceWindow* shown = WorkspaceWindow::From(window);
  const WorkspaceList::Workspace* workspace = WorkspaceOf(window);
  if (now == before || !workspace || !workspace->theme) {
    return;
  }
  Theme theme = *workspace->theme;
  if (now.colors != before.colors || now.intensity != before.intensity) {
    theme.colors = now.colors;
    theme.intensity = now.intensity;
  }
  if (now.grain != before.grain) {
    theme.grain = now.grain;
  }
  if (now.colors_in_dark_mode != before.colors_in_dark_mode) {
    theme.colors_in_dark_mode = now.colors_in_dark_mode;
  }
  if (now.mode != before.mode) {
    theme.mode = now.mode;
  }
  if (theme == *workspace->theme) {
    return;
  }
  const std::string id = shown->workspace();
  WorkspaceList::Get().SetTheme(id, theme);
  Redraw(id);
}

void StartWindowTheme(BrowserWindowInterface* window) {
  Profile* profile = window->GetProfile();
  const Theme theme = ProfileTheme(profile);
  SeenThemes().try_emplace(profile->GetPath(), theme);
  const WorkspaceWindow* shown = WorkspaceWindow::From(window);
  const WorkspaceList::Workspace* workspace = WorkspaceOf(window);
  if (workspace && !workspace->theme) {
    WorkspaceList::Get().SetTheme(shown->workspace(), theme);
  }
}

}  // namespace uw
