// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/browser/ui/webui/uw_theme_picker/uw_theme_picker_ui.h"

#include <algorithm>
#include <utility>
#include <vector>

#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/themes/theme_service.h"
#include "chrome/browser/themes/theme_service_factory.h"
#include "chrome/grit/generated_resources.h"
#include "chrome/grit/uw_theme_picker_resources.h"
#include "chrome/grit/uw_theme_picker_resources_map.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/web_ui.h"
#include "content/public/browser/web_ui_data_source.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "ui/base/mojom/themes.mojom.h"
#include "ui/base/webui/web_ui_util.h"
#include "ui/webui/webui_util.h"
#include "uw/components/look/theme.h"
#include "uw/components/look/theme_prefs.h"

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

}  // namespace

class UwThemePickerUI::Handler : public mojom::ThemePickerHandler {
 public:
  Handler(UwThemePickerUI* ui,
          Profile* profile,
          mojo::PendingReceiver<mojom::ThemePickerHandler> receiver)
      : ui_(ui), profile_(profile), receiver_(this, std::move(receiver)) {}

  // mojom::ThemePickerHandler:
  void GetState(GetStateCallback callback) override {
    const Theme theme =
        ReadTheme(*profile_->GetPrefs(), themes()->GetUserColor());
    auto state = mojom::Theme::New(ToMojo(theme.colors), theme.intensity,
                                   theme.grain, theme.colors_in_dark_mode);
    std::vector<std::vector<uint32_t>> suggested;
    for (const std::vector<SkColor>& colors : SuggestedThemes()) {
      suggested.push_back(ToMojo(colors));
    }
    std::move(callback).Run(std::move(state), Mode(), std::move(suggested));
  }

  void SetTheme(mojom::ThemePtr picked) override {
    Theme theme;
    if (!picked->colors.empty()) {
      theme.colors.assign(picked->colors.begin(),
                          picked->colors.begin() +
                              std::min(picked->colors.size(), kMaxThemeColors));
    }
    theme.intensity = std::clamp(picked->intensity, 0, kFullIntensity);
    theme.grain = std::clamp(picked->grain, 0, kFullGrain);
    theme.colors_in_dark_mode = picked->colors_in_dark_mode;
    WriteTheme(*profile_->GetPrefs(), theme);
    themes()->SetUserColorAndBrowserColorVariant(
        WithIntensity(theme.colors[0], theme.intensity),
        ui::mojom::BrowserColorVariant::kTonalSpot);
  }

  void SetMode(mojom::ColorMode mode) override {
    switch (mode) {
      case mojom::ColorMode::kAuto:
        themes()->SetBrowserColorScheme(
            ThemeService::BrowserColorScheme::kSystem);
        break;
      case mojom::ColorMode::kLight:
        themes()->SetBrowserColorScheme(
            ThemeService::BrowserColorScheme::kLight);
        break;
      case mojom::ColorMode::kDark:
        themes()->SetBrowserColorScheme(
            ThemeService::BrowserColorScheme::kDark);
        break;
    }
  }

  void ShowUI() override {
    if (auto embedder = ui_->embedder()) {
      embedder->ShowUI();
    }
  }

 private:
  ThemeService* themes() {
    return ThemeServiceFactory::GetForProfile(profile_);
  }

  mojom::ColorMode Mode() {
    switch (themes()->GetBrowserColorScheme()) {
      case ThemeService::BrowserColorScheme::kSystem:
        return mojom::ColorMode::kAuto;
      case ThemeService::BrowserColorScheme::kLight:
        return mojom::ColorMode::kLight;
      case ThemeService::BrowserColorScheme::kDark:
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
