// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/browser/ui/webui/uw_theme_picker/uw_theme_picker_ui.h"

#include "chrome/grit/uw_chat_resources.h"

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
#include "ui/webui/webui_util.h"
#include "uw/browser/ui/glass/glass.h"
#include "uw/browser/workspaces/workspace_theme.h"
#include "uw/components/look/theme.h"
#include "uw/components/look/theme_prefs.h"
#include "uw/components/look/tint.h"

namespace uw {
namespace {

constexpr webui::LocalizedString kStrings[] = {
    {"modeLight", IDS_UW_THEME_MODE_LIGHT},
    {"modeDark", IDS_UW_THEME_MODE_DARK},
    {"pages", IDS_UW_THEME_PAGES},
    {"pagesTheme", IDS_UW_THEME_PAGES_WINDOW},
    {"pagesSameAsTheme", IDS_UW_THEME_PAGES_SAME_AS_WINDOW},
    {"colorField", IDS_UW_THEME_COLOR_FIELD},
    {"addColor", IDS_UW_THEME_ADD_COLOR},
    {"suggested", IDS_UW_THEME_SUGGESTED},
    {"intensity", IDS_UW_THEME_INTENSITY},
    {"brightness", IDS_UW_THEME_BRIGHTNESS},
    {"grain", IDS_UW_THEME_GRAIN},
    {"transparency", IDS_UW_THEME_TRANSPARENCY},
    {"glass", IDS_UW_THEME_GLASS},
    {"glassTick", IDS_UW_THEME_GLASS_TICK},
    {"glassEnergySaver", IDS_UW_THEME_GLASS_ENERGY_SAVER},
    {"glassReduceTransparency", IDS_UW_THEME_GLASS_REDUCE_TRANSPARENCY},
    {"suggestion0", IDS_UW_THEME_LILAC_TO_SKY},
    {"suggestion1", IDS_UW_THEME_LILAC},
    {"suggestion2", IDS_UW_THEME_SKY},
    {"suggestion3", IDS_UW_THEME_MINT},
    {"suggestion4", IDS_UW_THEME_BUTTER},
    {"suggestion5", IDS_UW_THEME_PEACH},
    {"suggestion6", IDS_UW_THEME_WINE},
    {"suggestion7", IDS_UW_THEME_FOREST},
    {"suggestion8", IDS_UW_THEME_BLACK},
    {"suggestion9", IDS_UW_THEME_PEACH_TO_BUTTER},
    {"suggestion10", IDS_UW_THEME_MINT_TO_SKY},
};

std::vector<uint32_t> ToMojo(const std::vector<SkColor>& colors) {
  return std::vector<uint32_t>(colors.begin(), colors.end());
}

PageMode PageModeOf(mojom::PageMode mode) {
  switch (mode) {
    case mojom::PageMode::kTheme:
      return PageMode::kTheme;
    case mojom::PageMode::kLight:
      return PageMode::kLight;
    case mojom::PageMode::kDark:
      return PageMode::kDark;
  }
}

mojom::PageMode ToMojo(PageMode mode) {
  switch (mode) {
    case PageMode::kTheme:
      return mojom::PageMode::kTheme;
    case PageMode::kLight:
      return mojom::PageMode::kLight;
    case PageMode::kDark:
      return mojom::PageMode::kDark;
  }
}

std::vector<SkColor> ColorsOf(const std::vector<uint32_t>& colors) {
  return std::vector<SkColor>(
      colors.begin(), colors.begin() + std::min(colors.size(), kMaxThemeColors));
}

std::vector<uint32_t> ShownColorsOf(const Theme& theme) {
  std::vector<uint32_t> shown;
  for (SkColor color : theme.colors) {
    shown.push_back(ColorAsShown(theme, color));
  }
  return shown;
}

std::vector<mojom::FieldPointPtr> FieldPointsOf(
    const std::vector<SkColor>& colors) {
  std::vector<mojom::FieldPointPtr> points;
  for (SkColor color : colors) {
    const FieldPoint point = FieldPointOf(color);
    points.push_back(mojom::FieldPoint::New(point.x, point.y));
  }
  return points;
}

// The glass sliders for `theme`. The tick sits where its text stops reading.
mojom::GlassSlidersPtr GlassSlidersFor(const Theme& theme) {
  const SkColor text =
      TextColorOn(TintFrom(ColorAsShown(theme, theme.colors[0])));
  using performance_manager::user_tuning::BatterySaverModeManager;
  mojom::GlassPause pause = mojom::GlassPause::kNone;
  if (ReducesTransparency()) {
    pause = mojom::GlassPause::kReduceTransparency;
  } else if (CanShowGlass() && BatterySaverModeManager::HasInstance() &&
             BatterySaverModeManager::GetInstance()->IsBatterySaverActive()) {
    pause = mojom::GlassPause::kEnergySaver;
  }
  return mojom::GlassSliders::New(CanShowGlass(), CanShowElementGlass(),
                                  ReadableTransparency(text), pause);
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
                                   theme.brightness, theme.grain,
                                   theme.transparency, theme.glass);
    std::vector<mojom::SuggestedThemePtr> suggested;
    for (const SuggestedTheme& suggestion : SuggestedThemes()) {
      Theme suggested_theme;
      suggested_theme.colors = suggestion.colors;
      suggested_theme.brightness = suggestion.brightness;
      suggested.push_back(mojom::SuggestedTheme::New(
          ToMojo(suggestion.colors), suggestion.brightness,
          ShownColorsOf(suggested_theme)));
    }
    std::move(callback).Run(std::move(state), ShownColorsOf(theme),
                            FieldPointsOf(theme.colors),
                            ToMojo(theme.page_mode), std::move(suggested),
                            GlassSlidersFor(theme));
  }

  void ColorAt(mojom::FieldPointPtr point, ColorAtCallback callback) override {
    std::move(callback).Run(FieldColorAt({point->x, point->y}));
  }

  void PointsOf(const std::vector<uint32_t>& colors,
                PointsOfCallback callback) override {
    std::move(callback).Run(FieldPointsOf(ColorsOf(colors)));
  }

  void SetTheme(mojom::ThemePtr picked, SetThemeCallback callback) override {
    BrowserWindowInterface* window = Window();
    Theme theme = window ? ThemeOfWindow(window) : Theme();
    if (!picked->colors.empty()) {
      theme.colors = ColorsOf(picked->colors);
    }
    theme.intensity = std::clamp(picked->intensity, 0, kFullIntensity);
    theme.brightness = std::clamp(picked->brightness, 0, kFullBrightness);
    theme.grain = std::clamp(picked->grain, 0, kFullGrain);
    theme.transparency = std::clamp(picked->transparency, 0, kFullTransparency);
    theme.glass = std::clamp(picked->glass, 0, kFullGlass);
    if (window) {
      SetThemeOfWindow(window, theme);
    }
    std::move(callback).Run(ShownColorsOf(theme), GlassSlidersFor(theme));
  }

  void SetPageMode(mojom::PageMode mode) override {
    BrowserWindowInterface* window = Window();
    if (!window) {
      return;
    }
    Theme theme = ThemeOfWindow(window);
    theme.page_mode = PageModeOf(mode);
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
  source->AddResourcePath("problems.js", IDR_UW_CHAT_PROBLEMS_JS);
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

void UwThemePickerUI::BindInterface(
    mojo::PendingReceiver<mojom::ProblemReporter> receiver) {
  problem_reporter_ =
      std::make_unique<ProblemReporter>(web_ui(), std::move(receiver));
}

WEB_UI_CONTROLLER_TYPE_IMPL(UwThemePickerUI)

}  // namespace uw
