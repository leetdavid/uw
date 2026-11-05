// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/browser/workspaces/workspace_theme.h"

#include <algorithm>
#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "base/auto_reset.h"
#include "base/no_destructor.h"
#include "chrome/browser/picture_in_picture/picture_in_picture_window_manager.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/themes/theme_service.h"
#include "chrome/browser/themes/theme_service_factory.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface_iterator.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/picture_in_picture_window_controller.h"
#include "content/public/browser/web_contents.h"
#include "ui/base/base_window.h"
#include "ui/base/mojom/themes.mojom.h"
#include "ui/color/color_provider_key.h"
#include "ui/native_theme/native_theme.h"
#include "ui/views/widget/widget.h"
#include "uw/browser/ui/look/page_color.h"
#include "uw/browser/ui/look/theme_colors.h"
#include "uw/browser/workspaces/workspace_list.h"
#include "uw/browser/workspaces/workspace_window.h"
#include "uw/components/look/theme_prefs.h"
#include "uw/components/look/tint.h"

namespace uw {
namespace {

// Set while uw saves a theme, whose writes to Chromium's settings are its own.
bool g_saving = false;

// The theme of each profile as uw last saw it, by the profile's folder.
std::map<base::FilePath, Theme>& SeenThemes() {
  static base::NoDestructor<std::map<base::FilePath, Theme>> seen;
  return *seen;
}

// The theme Chromium's settings for `profile` hold, with uw's own parts.
Theme ProfileTheme(Profile* profile) {
  return ReadTheme(*profile->GetPrefs(),
                   ThemeServiceFactory::GetForProfile(profile)->GetUserColor());
}

// The front window that shows workspace `id`, or null.
BrowserWindowInterface* WindowOfWorkspace(std::string_view id) {
  BrowserWindowInterface* found = nullptr;
  ForEachCurrentBrowserWindowInterfaceOrderedByActivation(
      [&](BrowserWindowInterface* window) {
        const WorkspaceWindow* shown = WorkspaceWindow::From(window);
        if (shown && shown->workspace() == id && !shown->hidden()) {
          found = window;
          return false;
        }
        return true;
      });
  return found;
}

// The workspace that `window` shows, or null.
const WorkspaceList::Workspace* WorkspaceOf(BrowserWindowInterface* window) {
  const WorkspaceWindow* shown = WorkspaceWindow::From(window);
  return shown ? WorkspaceList::Get().Find(shown->workspace()) : nullptr;
}

// Sets the colors of `theme` in `key`.
void SetThemeColors(const Theme& theme, ui::ColorProviderKey& key) {
  using Source = ui::ColorProviderKey::UserColorSource;
  // Until a color is picked, Chromium keeps its own colors, as it does for a
  // profile with none.
  if (theme.colors == Theme().colors && theme.intensity == kFullIntensity &&
      theme.brightness == kFullBrightness) {
    key.user_color = std::nullopt;
    key.scheme_variant = std::nullopt;
    key.user_color_source = Source::kBaseline;
    return;
  }
  const SkColor first = ColorAsShown(theme, theme.colors[0]);
  key.user_color = first;
  key.scheme_variant = ui::ColorProviderKey::SchemeVariant::kTonalSpot;
  // A color with no hue of its own, such as black, gives Chromium's neutral
  // colors.
  key.user_color_source = IsNeutral(first) ? Source::kGrayscale : Source::kAccent;
}

ui::ColorProviderKey::ColorMode ModeOfTheme(const Theme& theme) {
  return IsDarkTheme(theme) ? ui::ColorProviderKey::ColorMode::kDark
                            : ui::ColorProviderKey::ColorMode::kLight;
}

// Whether the mode of `profile`, which is Chromium's own light or dark
// setting, is dark, with Auto following the system.
bool ModeIsDark(Profile* profile) {
  switch (ThemeServiceFactory::GetForProfile(profile->GetOriginalProfile())
              ->GetBrowserColorScheme()) {
    case ThemeService::BrowserColorScheme::kSystem:
      return ui::NativeTheme::GetInstanceForWeb()->preferred_color_scheme() ==
             ui::NativeTheme::PreferredColorScheme::kDark;
    case ThemeService::BrowserColorScheme::kLight:
      return false;
    case ThemeService::BrowserColorScheme::kDark:
      return true;
  }
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

// A site's own picture-in-picture window, such as a Meet call, wears the
// theme of the window it came from, per docs/plans/mini-player.md.
BrowserWindowInterface* WindowOfTheme(BrowserWindowInterface* window) {
  if (window->GetType() != BrowserWindowInterface::TYPE_PICTURE_IN_PICTURE) {
    return window;
  }
  // A video's picture in picture is no browser window, so while a site's
  // window is kept beside one, this is that site's window.
  PictureInPictureWindowManager* manager =
      PictureInPictureWindowManager::GetInstance();
  content::PictureInPictureWindowController* kept =
      manager->kept_window_controller();
  content::WebContents* opener =
      kept ? kept->GetWebContents() : manager->GetWebContents();
  tabs::TabInterface* tab =
      opener ? tabs::TabInterface::MaybeGetFromContents(opener) : nullptr;
  return tab ? tab->GetBrowserWindowInterface() : window;
}

}  // namespace

Theme ThemeOfWindow(BrowserWindowInterface* window) {
  const WorkspaceList::Workspace* workspace =
      WorkspaceOf(WindowOfTheme(window));
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
        ColorAsShown(theme, theme.colors[0]),
        ui::mojom::BrowserColorVariant::kTonalSpot);
  }
  SeenThemes()[profile->GetPath()] = ProfileTheme(profile);
  if (!workspace.empty()) {
    Redraw(workspace);
  }
}

Theme NewWorkspaceTheme(const Theme& like) {
  Theme theme;
  theme.brightness = like.brightness;
  const std::vector<SuggestedTheme> suggested = SuggestedThemes();
  theme.colors = suggested.front().colors;
  for (const SuggestedTheme& suggestion : suggested) {
    const bool used = std::ranges::any_of(
        WorkspaceList::Get().workspaces(),
        [&](const WorkspaceList::Workspace& workspace) {
          return workspace.theme &&
                 workspace.theme->colors == suggestion.colors;
        });
    if (!used) {
      theme.colors = suggestion.colors;
      break;
    }
  }
  return theme;
}

void ApplyThemeOfWindow(BrowserWindowInterface* window,
                        ui::ColorProviderKey& key) {
  // A private window keeps Chromium's dark look, and a web app its own.
  const BrowserWindowInterface::Type type = window->GetType();
  if (key.custom_theme || window->GetProfile()->IsOffTheRecord() ||
      type == BrowserWindowInterface::TYPE_APP ||
      type == BrowserWindowInterface::TYPE_APP_POPUP) {
    return;
  }
  const Theme theme = ThemeOfWindow(window);
  key.color_mode = ModeOfTheme(theme);
  SetThemeColors(theme, key);
  // The mode drives everything but the sidebar, which keeps the theme's
  // colors. Dark takes Chromium's neutral colors and uw's blacks.
  const ui::ColorProviderKey theme_key = key;
  const bool dark = ModeIsDark(window->GetProfile());
  key.color_mode = dark ? ui::ColorProviderKey::ColorMode::kDark
                        : ui::ColorProviderKey::ColorMode::kLight;
  if (dark) {
    key.user_color_source = ui::ColorProviderKey::UserColorSource::kGrayscale;
  }
  if (key == theme_key) {
    return;
  }
  key.app_controller = ThemeColorMixers(
      theme_key, PageColorOfMixers(theme_key.app_controller.get()));
}

std::optional<SkColor> EmptyPageColor(content::WebContents* page) {
  if (!page) {
    return std::nullopt;
  }
  return ModeIsDark(Profile::FromBrowserContext(page->GetBrowserContext()))
             ? kBlackCard
             : SK_ColorWHITE;
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
  if (now.colors != before.colors || now.intensity != before.intensity ||
      now.brightness != before.brightness) {
    theme.colors = now.colors;
    theme.intensity = now.intensity;
    theme.brightness = now.brightness;
  }
  if (now.grain != before.grain) {
    theme.grain = now.grain;
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
  // The Pages row's Light or Dark, from before the mode was the profile's,
  // starts it.
  if (const std::optional<bool> dark =
          TakeOldPagesRowDark(*profile->GetPrefs())) {
    ThemeServiceFactory::GetForProfile(profile)->SetBrowserColorScheme(
        *dark ? ThemeService::BrowserColorScheme::kDark
              : ThemeService::BrowserColorScheme::kLight);
  }
  const Theme theme = ProfileTheme(profile);
  SeenThemes().try_emplace(profile->GetPath(), theme);
  const WorkspaceWindow* shown = WorkspaceWindow::From(window);
  const WorkspaceList::Workspace* workspace = WorkspaceOf(window);
  if (workspace && !workspace->theme) {
    WorkspaceList::Get().SetTheme(shown->workspace(), theme);
  }
}

Theme ThemeOfWorkspace(std::string_view id, Profile* profile) {
  if (BrowserWindowInterface* window = WindowOfWorkspace(id)) {
    return ThemeOfWindow(window);
  }
  const WorkspaceList::Workspace* workspace = WorkspaceList::Get().Find(id);
  return workspace && workspace->theme ? *workspace->theme
                                       : ProfileTheme(profile);
}

void SetThemeOfWorkspace(std::string_view id, const Theme& theme) {
  if (BrowserWindowInterface* window = WindowOfWorkspace(id)) {
    SetThemeOfWindow(window, theme);
    return;
  }
  WorkspaceList::Get().SetTheme(id, theme);
}

}  // namespace uw
