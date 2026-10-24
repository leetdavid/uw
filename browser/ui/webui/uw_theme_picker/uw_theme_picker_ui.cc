// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/browser/ui/webui/uw_theme_picker/uw_theme_picker_ui.h"

#include <algorithm>
#include <utility>
#include <vector>

#include "chrome/browser/performance_manager/public/user_tuning/battery_saver_mode_manager.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/themes/theme_service.h"
#include "chrome/browser/themes/theme_service_factory.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface_iterator.h"
#include "chrome/grit/generated_resources.h"
#include "chrome/grit/uw_theme_picker_resources.h"
#include "chrome/grit/uw_theme_picker_resources_map.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/web_ui.h"
#include "content/public/browser/web_ui_data_source.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "ui/base/mojom/themes.mojom.h"
#include "ui/base/webui/web_ui_util.h"
#include "ui/native_theme/native_theme.h"
#include "ui/webui/webui_util.h"
#include "uw/browser/ui/glass/glass.h"
#include "uw/browser/workspaces/workspace_theme.h"
#include "uw/components/look/theme.h"
#include "uw/components/look/theme_prefs.h"
#include "uw/components/look/tint.h"

namespace uw {
namespace {

constexpr webui::LocalizedString kStrings[] = {
    {"modeAuto", IDS_UW_THEME_MODE_AUTO},
    {"modeLight", IDS_UW_THEME_MODE_LIGHT},
    {"modeDark", IDS_UW_THEME_MODE_DARK},
    {"colorField", IDS_UW_THEME_COLOR_FIELD},
    {"addColor", IDS_UW_THEME_ADD_COLOR},
    {"suggested", IDS_UW_THEME_SUGGESTED},
    {"intensity", IDS_UW_THEME_INTENSITY},
    {"grain", IDS_UW_THEME_GRAIN},
    {"transparency", IDS_UW_THEME_TRANSPARENCY},
    {"glass", IDS_UW_THEME_GLASS},
    {"glassTick", IDS_UW_THEME_GLASS_TICK},
    {"glassEnergySaver", IDS_UW_THEME_GLASS_ENERGY_SAVER},
    {"glassReduceTransparency", IDS_UW_THEME_GLASS_REDUCE_TRANSPARENCY},
    {"colorsInDarkMode", IDS_UW_THEME_COLORS_IN_DARK_MODE},
    {"suggestion0", IDS_UW_THEME_LILAC},
    {"suggestion1", IDS_UW_THEME_SKY},
    {"suggestion2", IDS_UW_THEME_MINT},
    {"suggestion3", IDS_UW_THEME_BUTTER},
    {"suggestion4", IDS_UW_THEME_PEACH},
    {"suggestion5", IDS_UW_THEME_WINE},
    {"suggestion6", IDS_UW_THEME_FOREST},
    {"suggestion7", IDS_UW_THEME_LILAC_TO_SKY},
    {"suggestion8", IDS_UW_THEME_PEACH_TO_BUTTER},
    {"suggestion9", IDS_UW_THEME_MINT_TO_SKY},
};

std::vector<uint32_t> ToMojo(const std::vector<SkColor>& colors) {
  return std::vector<uint32_t>(colors.begin(), colors.end());
}

ThemeMode ThemeModeOf(mojom::ColorMode mode) {
  switch (mode) {
    case mojom::ColorMode::kAuto:
      return ThemeMode::kAuto;
    case mojom::ColorMode::kLight:
      return ThemeMode::kLight;
    case mojom::ColorMode::kDark:
      return ThemeMode::kDark;
  }
}

// The glass sliders for a theme whose first color, at its intensity, is
// `first`, in `mode`. The tick sits where the theme's text stops reading.
mojom::GlassSlidersPtr GlassSlidersFor(SkColor first, ThemeMode mode) {
  const bool dark =
      mode == ThemeMode::kDark ||
      (mode == ThemeMode::kAuto &&
       ui::NativeTheme::GetInstanceForNativeUi()->preferred_color_scheme() ==
           ui::NativeTheme::PreferredColorScheme::kDark);
  const SkColor text = dark ? kLightTextOnTint : TextColorOn(TintFrom(first));
  using performance_manager::user_tuning::BatterySaverModeManager;
  mojom::GlassPause pause = mojom::GlassPause::kNone;
  if (ReducesTransparency()) {
    pause = mojom::GlassPause::kReduceTransparency;
  } else if (BatterySaverModeManager::HasInstance() &&
             BatterySaverModeManager::GetInstance()->IsBatterySaverActive()) {
    pause = mojom::GlassPause::kEnergySaver;
  }
  return mojom::GlassSliders::New(CanShowGlass(), ReadableTransparency(text),
                                  pause);
}

}  // namespace

bool UwThemePickerUIConfig::ShouldAutoResizeHost() {
  return true;
}

class UwThemePickerUI::Handler : public mojom::ThemePickerHandler {
 public:
  Handler(UwThemePickerUI* ui,
          Profile* profile,
          mojo::PendingReceiver<mojom::ThemePickerHandler> receiver)
      : ui_(ui), profile_(profile), receiver_(this, std::move(receiver)) {}

  // mojom::ThemePickerHandler:
  void GetState(GetStateCallback callback) override {
    BrowserWindowInterface* window = Window();
    const Theme theme = window ? ThemeOfWindow(window) : Theme();
    auto state = mojom::Theme::New(ToMojo(theme.colors), theme.intensity,
                                   theme.grain, theme.transparency, theme.glass,
                                   theme.colors_in_dark_mode);
    std::vector<std::vector<uint32_t>> suggested;
    for (const std::vector<SkColor>& colors : SuggestedThemes()) {
      suggested.push_back(ToMojo(colors));
    }
    std::move(callback).Run(
        std::move(state), ModeOf(theme.mode), std::move(suggested),
        GlassSlidersFor(WithIntensity(theme.colors[0], theme.intensity),
                        theme.mode));
  }

  void GetGlassSliders(mojom::ThemePtr theme,
                       mojom::ColorMode mode,
                       GetGlassSlidersCallback callback) override {
    const SkColor first =
        theme->colors.empty() ? Theme().colors[0] : theme->colors[0];
    std::move(callback).Run(GlassSlidersFor(
        WithIntensity(first, std::clamp(theme->intensity, 0, kFullIntensity)),
        ThemeModeOf(mode)));
  }

  void SetTheme(mojom::ThemePtr picked) override {
    BrowserWindowInterface* window = Window();
    if (!window) {
      return;
    }
    Theme theme = ThemeOfWindow(window);
    if (!picked->colors.empty()) {
      theme.colors.assign(picked->colors.begin(),
                          picked->colors.begin() +
                              std::min(picked->colors.size(), kMaxThemeColors));
    }
    theme.intensity = std::clamp(picked->intensity, 0, kFullIntensity);
    theme.grain = std::clamp(picked->grain, 0, kFullGrain);
    theme.transparency = std::clamp(picked->transparency, 0, kFullTransparency);
    theme.glass = std::clamp(picked->glass, 0, kFullGlass);
    theme.colors_in_dark_mode = picked->colors_in_dark_mode;
    SetThemeOfWindow(window, theme);
  }

  void SetMode(mojom::ColorMode mode) override {
    BrowserWindowInterface* window = Window();
    if (!window) {
      return;
    }
    Theme theme = ThemeOfWindow(window);
    theme.mode = ThemeModeOf(mode);
    SetThemeOfWindow(window, theme);
  }

  void ShowUI() override {
    if (auto embedder = ui_->embedder()) {
      embedder->ShowUI();
    }
  }

 private:
  // The window the picker opened in: the profile's front one.
  BrowserWindowInterface* Window() {
    BrowserWindowInterface* found = nullptr;
    ForEachCurrentBrowserWindowInterfaceOrderedByActivation(
        [&](BrowserWindowInterface* window) {
          if (window->GetProfile() == profile_) {
            found = window;
            return false;
          }
          return true;
        });
    return found;
  }

  static mojom::ColorMode ModeOf(ThemeMode mode) {
    switch (mode) {
      case ThemeMode::kAuto:
        return mojom::ColorMode::kAuto;
      case ThemeMode::kLight:
        return mojom::ColorMode::kLight;
      case ThemeMode::kDark:
        return mojom::ColorMode::kDark;
    }
  }

  const raw_ptr<UwThemePickerUI> ui_;
  const raw_ptr<Profile> profile_;
  mojo::Receiver<mojom::ThemePickerHandler> receiver_;
};

UwThemePickerUI::UwThemePickerUI(content::WebUI* web_ui)
    : TopChromeWebUIController(web_ui) {
  content::WebUIDataSource* source = content::WebUIDataSource::CreateAndAdd(
      Profile::FromWebUI(web_ui), kThemePickerHost);
  webui::SetupWebUIDataSource(source, kUwThemePickerResources,
                              IDR_UW_THEME_PICKER_THEME_PICKER_HTML);
  source->AddLocalizedStrings(kStrings);
}

UwThemePickerUI::~UwThemePickerUI() = default;

void UwThemePickerUI::BindInterface(
    mojo::PendingReceiver<mojom::ThemePickerHandlerFactory> receiver) {
  factory_.reset();
  factory_.Bind(std::move(receiver));
}

void UwThemePickerUI::CreateHandler(
    mojo::PendingReceiver<mojom::ThemePickerHandler> handler) {
  handler_ = std::make_unique<Handler>(this, Profile::FromWebUI(web_ui()),
                                       std::move(handler));
}

WEB_UI_CONTROLLER_TYPE_IMPL(UwThemePickerUI)

}  // namespace uw
