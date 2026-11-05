// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/browser/ui/look/page_color.h"

#include <algorithm>
#include <map>
#include <memory>

#include "base/containers/flat_map.h"
#include "base/no_destructor.h"
#include "ui/color/color_mixer.h"
#include "ui/color/color_provider.h"
#include "ui/color/color_provider_manager.h"
#include "ui/color/color_recipe.h"
#include "ui/gfx/color_utils.h"
#include "uw/components/look/tint.h"

namespace uw {
namespace {

// How strongly the text color shows over the page color.
constexpr float kIcon = 0.75f;
constexpr float kDisabled = 0.35f;
constexpr float kDimmedText = 0.62f;
constexpr float kLine = 0.10f;
constexpr float kFieldOnLight = 0.06f;
constexpr float kFieldOnDark = 0.10f;
constexpr float kFieldHoveredOnLight = 0.09f;
constexpr float kFieldHoveredOnDark = 0.14f;
constexpr SkAlpha kInkDropHover = 0x14;
constexpr SkAlpha kInkDropPressed = 0x29;

// Chromium keeps each set of colors it made, under the address of the mixers
// that made it, so mixers live until that store is cleared. Past this many
// page colors it is cleared and the unused mixers go.
constexpr size_t kMostMixers = 32;

// The toolbar's color with no page color: white in light mode, #0A0A0A in dark
// mode. A page of that color keeps today's toolbar exactly.
SkColor TodaysToolbar(const ui::ColorProviderKey& key) {
  ui::ColorProviderKey today = key;
  today.app_controller = nullptr;
  return ui::ColorProviderManager::Get().GetColorProviderFor(today)->GetColor(
      kColorToolbar);
}

class Mixers : public ui::ColorProviderKey::InitializerSupplier {
 public:
  explicit Mixers(SkColor page) : page_(page) {}
  ~Mixers() override = default;

  void AddColorMixers(ui::ColorProvider* provider,
                      const ui::ColorProviderKey& key) const override {
    AddPageColorMixers(provider, key, page_);
  }

 private:
  const SkColor page_;
};

struct PageColors {
  base::flat_map<const views::View*, SkColor> windows;
  std::map<SkColor, std::unique_ptr<Mixers>> mixers;
};

PageColors& GetPageColors() {
  static base::NoDestructor<PageColors> page_colors;
  return *page_colors;
}

}  // namespace

void AddPageColorMixers(ui::ColorProvider* provider,
                        const ui::ColorProviderKey& key,
                        SkColor page) {
  if (key.custom_theme || page == TodaysToolbar(key)) {
    return;
  }
  const SkColor text = TextColorOn(Tint{page, page});
  const bool white = text == kLightTextOnTint;
  const auto over = [&](float strength) {
    return color_utils::AlphaBlend(text, page, strength);
  };
  const SkColor icon = over(kIcon);
  const SkColor disabled = over(kDisabled);
  const SkColor line = over(kLine);
  const SkColor field = over(white ? kFieldOnDark : kFieldOnLight);
  const SkColor field_hovered =
      over(white ? kFieldHoveredOnDark : kFieldHoveredOnLight);
  const SkColor hover = SkColorSetA(text, kInkDropHover);
  const SkColor pressed = SkColorSetA(text, kInkDropPressed);

  ui::ColorMixer& mixer = provider->AddMixer();
  const auto set = [&](ui::ColorId id, SkColor color) { mixer[id] = {color}; };
  set(kColorToolbar, page);
  set(kColorToolbarText, text);
  set(kColorToolbarTextDisabled, disabled);
  set(kColorToolbarButtonIcon, icon);
  set(kColorToolbarButtonIconInactive, disabled);
  set(kColorToolbarButtonIconDisabled, disabled);
  set(kColorToolbarButtonText, icon);
  set(kColorToolbarInkDrop, text);
  set(kColorToolbarInkDropHover, hover);
  set(kColorToolbarInkDropRipple, pressed);
  set(kColorToolbarContentAreaSeparator, line);
  set(kColorToolbarExtensionSeparatorEnabled, line);

  set(kColorLocationBarBackground, field);
  set(kColorLocationBarBackgroundHovered, field_hovered);
  set(kColorPageFieldFocused, field);
  set(kColorPageFieldFocusedHovered, field_hovered);
  set(kColorPageFieldText, text);
  set(kColorPageFieldTextDimmed, over(kDimmedText));
  set(kColorOmniboxIconForeground, icon);
  set(kColorOmniboxIconForegroundTonal, icon);
  set(kColorOmniboxIconBackground, field_hovered);
  set(kColorOmniboxIconBackgroundTonal, field_hovered);
  set(kColorOmniboxIconHover, hover);
  set(kColorOmniboxActionIcon, icon);
  set(kColorOmniboxActionIconHover, hover);
  set(kColorOmniboxIconPressed, pressed);

  set(kColorBookmarkBarBackground, page);
  set(kColorBookmarkBarForeground, text);
  set(kColorBookmarkBarForegroundDisabled, disabled);
  set(kColorBookmarkBarSeparator, line);
  set(kColorBookmarkBarSeparatorChromeRefresh, line);
  set(kColorBookmarkButtonIcon, icon);
  set(kColorBookmarkFolderIcon, icon);
}

void SetPageColor(const views::View* browser_view,
                  std::optional<SkColor> color) {
  PageColors& page_colors = GetPageColors();
  if (!color) {
    page_colors.windows.erase(browser_view);
    return;
  }
  page_colors.windows[browser_view] = *color;
  if (page_colors.mixers.contains(*color)) {
    return;
  }
  if (page_colors.mixers.size() >= kMostMixers) {
    ui::ColorProviderManager::Get().ResetColorProviderCache();
    std::erase_if(page_colors.mixers, [&](const auto& entry) {
      return std::ranges::none_of(page_colors.windows, [&](const auto& window) {
        return window.second == entry.first;
      });
    });
  }
  page_colors.mixers.emplace(*color, std::make_unique<Mixers>(*color));
}

ui::ColorProviderKey::InitializerSupplier* PageColorMixers(
    const views::View* browser_view) {
  PageColors& page_colors = GetPageColors();
  const auto found = page_colors.windows.find(browser_view);
  if (found == page_colors.windows.end()) {
    return nullptr;
  }
  return page_colors.mixers.at(found->second).get();
}

std::optional<SkColor> PageColorOfMixers(
    const ui::ColorProviderKey::InitializerSupplier* mixers) {
  for (const auto& [color, each] : GetPageColors().mixers) {
    if (each.get() == mixers) {
      return color;
    }
  }
  return std::nullopt;
}

void AddPageFieldDefaults(ui::ColorMixer& mixer) {
  mixer[kColorPageFieldText] = {kColorOmniboxText};
  mixer[kColorPageFieldTextDimmed] = {kColorOmniboxTextDimmed};
  mixer[kColorPageFieldFocused] = {kColorOmniboxResultsBackground};
  mixer[kColorPageFieldFocusedHovered] = {
      kColorOmniboxResultsBackgroundHovered};
}

}  // namespace uw
