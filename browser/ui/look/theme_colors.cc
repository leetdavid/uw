// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/browser/ui/look/theme_colors.h"

#include <algorithm>
#include <iterator>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

#include "base/no_destructor.h"
#include "chrome/browser/ui/color/chrome_color_id.h"
#include "ui/color/color_id.h"
#include "ui/color/color_mixer.h"
#include "ui/color/color_provider.h"
#include "ui/color/color_provider_manager.h"
#include "ui/color/color_provider_utils.h"
#include "ui/color/color_recipe.h"
#include "uw/browser/ui/look/menu_card.h"
#include "uw/browser/ui/look/page_card.h"
#include "uw/browser/ui/look/page_color.h"

namespace uw {
namespace {

// Chromium's colors of the sidebar and the frame around the page card, by
// name, so that colors Chromium adds to these families come along.
constexpr std::string_view kThemeColorFamilies[] = {
    "kColorSysHeader",      "kColorSysOnHeader",
    "kColorSysStateHeader", "kColorSysStateOnHeader",
    "kColorFrame",          "kColorWindowControlButton",
    "kColorCaptionButton",  "kColorNewTabButton",
    "kColorVerticalTab",    "kColorTabAlert",
    "kColorTabBackground",  "kColorTabCloseButton",
    "kColorTabDiscardRing", "kColorTabDivider",
    "kColorTabFocusRing",   "kColorTabForeground",
    "kColorTabGroup",       "kColorTabHoverCard",
    "kColorTabSearch",      "kColorTabStrip",
    "kColorTabstrip",       "kColorTabStroke",
    "kColorTabThrobber"};

// uw's own: the edge around the page card and uw's cards.
constexpr ui::ColorId kUwThemeColors[] = {
    kColorPageCardEdge,     kColorMenuCardBackground, kColorMenuCardHover,
    kColorMenuCardText,     kColorMenuCardIcon,       kColorMenuCardMuted,
    kColorMenuCardDisabled, kColorMenuCardLine,       kColorMenuCardChipHover,
    kColorMenuCardAccent,   kColorMenuCardGlass};

// Past this many sets of mixers, Chromium's store of colors is cleared and the
// unused sets go, as for page colors.
constexpr size_t kMostMixers = 32;

const std::vector<ui::ColorId>& ThemeColorIds() {
  static const base::NoDestructor<std::vector<ui::ColorId>> ids([] {
    std::vector<ui::ColorId> found(std::begin(kUwThemeColors),
                                   std::end(kUwThemeColors));
    for (ui::ColorId id = ui::kUiColorsStart; id < kChromeColorsEnd; ++id) {
      const std::string name = ui::ColorIdName(id);
      if (std::ranges::any_of(kThemeColorFamilies,
                              [&](std::string_view family) {
                                return name.starts_with(family);
                              })) {
        found.push_back(id);
      }
    }
    return found;
  }());
  return *ids;
}

class Mixers : public ui::ColorProviderKey::InitializerSupplier {
 public:
  Mixers(const ui::ColorProviderKey& theme_key,
         std::optional<SkColor> page_color)
      : theme_key_(theme_key), page_color_(page_color) {}
  ~Mixers() override = default;

  void AddColorMixers(ui::ColorProvider* provider,
                      const ui::ColorProviderKey& key) const override {
    if (page_color_) {
      AddPageColorMixers(provider, key, *page_color_);
    }
    const ui::ColorProvider* theme =
        ui::ColorProviderManager::Get().GetColorProviderFor(theme_key_);
    ui::ColorMixer& mixer = provider->AddMixer();
    for (ui::ColorId id : ThemeColorIds()) {
      mixer[id] = {theme->GetColor(id)};
    }
  }

 private:
  const ui::ColorProviderKey theme_key_;
  const std::optional<SkColor> page_color_;
};

// What tells two sets of mixers apart.
using MixersId = std::tuple<ui::ColorProviderKey::ColorMode,
                            ui::ColorProviderKey::UserColorSource,
                            std::optional<SkColor>,
                            std::optional<ui::ColorProviderKey::SchemeVariant>,
                            std::optional<SkColor>>;

std::map<MixersId, std::unique_ptr<Mixers>>& AllMixers() {
  static base::NoDestructor<std::map<MixersId, std::unique_ptr<Mixers>>> all;
  return *all;
}

}  // namespace

ui::ColorProviderKey::InitializerSupplier* ThemeColorMixers(
    const ui::ColorProviderKey& theme_key,
    std::optional<SkColor> page_color) {
  ui::ColorProviderKey key = theme_key;
  key.app_controller = nullptr;
  const MixersId id{key.color_mode, key.user_color_source, key.user_color,
                    key.scheme_variant, page_color};
  auto& all = AllMixers();
  if (const auto found = all.find(id); found != all.end()) {
    return found->second.get();
  }
  if (all.size() >= kMostMixers) {
    ui::ColorProviderManager::Get().ResetColorProviderCache();
    all.clear();
  }
  return all.emplace(id, std::make_unique<Mixers>(key, page_color))
      .first->second.get();
}

}  // namespace uw
