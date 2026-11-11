// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/browser/ui/look/tint_color_mixer.h"

#include "chrome/browser/ui/color/chrome_color_id.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/color/color_id.h"
#include "ui/color/color_mixer.h"
#include "ui/color/color_provider.h"
#include "ui/color/color_provider_key.h"
#include "ui/color/color_recipe.h"
#include "ui/color/color_transform.h"
#include "uw/browser/ui/look/menu_card.h"
#include "uw/browser/ui/look/page_card.h"
#include "uw/browser/ui/look/page_color.h"
#include "uw/browser/ui/look/top_button.h"
#include "uw/components/look/theme.h"
#include "uw/components/look/tint.h"

namespace uw {
namespace {

constexpr SkAlpha kDividerAlpha = 0x1A;
// Tiles, pills, and hovered rows: white at 46% on a light tint and 8% on a dark
// one, see-through so the theme shows through them.
constexpr SkAlpha kFillOnLightAlpha = 0x75;
constexpr SkAlpha kFillOnDarkAlpha = 0x14;

// The icons of the buttons at the top of the sidebar: the text color at 75%,
// and at 55% in a window that is not in front.
constexpr SkAlpha kTopButtonIconAlpha = 0xBF;
constexpr SkAlpha kTopButtonIconInactiveAlpha = 0x8C;

// uw's menu cards, light or on uw's blacks, as the window's light or dark
// says.
void AddMenuCardColors(ui::ColorMixer& mixer, bool dark) {
  mixer[kColorMenuCardBackground] = {dark ? kBlackCardField : SK_ColorWHITE};
  mixer[kColorMenuCardHover] = {dark ? kBlackCardEdge
                                     : SkColorSetRGB(0xF2, 0xF1, 0xF8)};
  mixer[kColorMenuCardText] = {dark ? SkColorSetRGB(0xED, 0xED, 0xED)
                                    : kDarkTextOnTint};
  mixer[kColorMenuCardIcon] = {dark ? SkColorSetRGB(0xA1, 0xA1, 0xA1)
                                    : SkColorSetRGB(0x4A, 0x47, 0x60)};
  mixer[kColorMenuCardMuted] = {dark ? SkColorSetRGB(0x73, 0x73, 0x73)
                                     : SkColorSetRGB(0x77, 0x74, 0x8C)};
  mixer[kColorMenuCardDisabled] = {dark ? SkColorSetRGB(0x52, 0x52, 0x52)
                                        : SkColorSetRGB(0xAD, 0xAA, 0xBB)};
  mixer[kColorMenuCardLine] = {dark ? kBlackCardEdge
                                    : SkColorSetARGB(0x17, 0x1E, 0x1B, 0x2E)};
  mixer[kColorMenuCardChipHover] = {dark ? SkColorSetRGB(0x33, 0x33, 0x33)
                                         : SkColorSetRGB(0xE4, 0xE2, 0xEF)};
  mixer[kColorMenuCardAccent] = {dark ? SkColorSetRGB(0x9D, 0xB0, 0xFF)
                                      : SkColorSetRGB(0x3B, 0x5B, 0xDB)};
  mixer[kColorMenuCardGlass] =
      ui::SetAlpha(kColorMenuCardBackground, kMenuCardOverGlass);
  mixer[kColorMenuGlass] =
      ui::SetAlpha(ui::kColorMenuBackground, kMenuCardOverGlass);
  mixer[kColorOmniboxResultsGlass] =
      ui::SetAlpha(kColorOmniboxResultsBackground, kMenuCardOverGlass);
}

// Custom themes bring their own frame and tab colors.
bool UsesTint(const ui::ColorProviderKey& key) {
  return !key.custom_theme;
}

bool IsDarkMode(const ui::ColorProviderKey& key) {
  return key.color_mode == ui::ColorProviderKey::ColorMode::kDark;
}

// The theme's first color, which lives in Chromium's user color, or the
// default.
SkColor FirstThemeColor(const ui::ColorProviderKey& key) {
  using Source = ui::ColorProviderKey::UserColorSource;
  // Chromium's neutral colors for a color with a hue, as in a private window.
  if (key.user_color_source == Source::kGrayscale &&
      !(key.user_color && IsNeutral(*key.user_color))) {
    return IsDarkMode(key) ? SK_ColorBLACK : SK_ColorWHITE;
  }
  if (key.user_color && key.user_color_source != Source::kBaseline) {
    return *key.user_color;
  }
  return kDefaultLightTint;
}

Tint TintFor(const ui::ColorProviderKey& key) {
  return TintFrom(FirstThemeColor(key));
}

}  // namespace

void AddTintColorMixer(ui::ColorProvider* provider,
                       const ui::ColorProviderKey& key) {
  if (!UsesTint(key)) {
    return;
  }
  const Tint tint = TintFor(key);
  const SkColor text = TextColorOn(tint);
  const SkColor fill = SkColorSetA(
      SK_ColorWHITE, IsDark(tint) ? kFillOnDarkAlpha : kFillOnLightAlpha);
  ui::ColorMixer& mixer = provider->AddMixer();
  for (ui::ColorId frame :
       {ui::kColorSysHeader, ui::kColorSysHeaderInactive, ui::kColorFrameActive,
        ui::kColorFrameActiveUnthemed, ui::kColorFrameInactive}) {
    mixer[frame] = {tint.start};
  }
  mixer[ui::kColorSysHeaderContainer] = {fill};
  mixer[ui::kColorSysHeaderContainerInactive] = {fill};
  mixer[ui::kColorSysOnHeaderPrimary] = {text};
  mixer[ui::kColorSysOnHeaderPrimaryInactive] = {text};
  mixer[ui::kColorSysOnHeaderDivider] = {SkColorSetA(text, kDividerAlpha)};
  mixer[ui::kColorSysOnHeaderDividerInactive] = {
      SkColorSetA(text, kDividerAlpha)};
  mixer[ui::kColorSysStateHeaderHover] = {fill};
  mixer[ui::kColorSysStateHeaderHoverInactive] = {fill};
  mixer[ui::kColorSysStateOnHeaderHover] = {fill};
}

void AddTintTabColorMixer(ui::ColorProvider* provider,
                          const ui::ColorProviderKey& key) {
  ui::ColorMixer& mixer = provider->AddMixer();
  mixer[kColorPageCardEdge] = {SK_ColorTRANSPARENT};
  AddPageFieldDefaults(mixer);
  AddMenuCardColors(mixer, IsDarkMode(key));
  mixer[kColorTopButtonIcon] =
      ui::SetAlpha(kColorTabForegroundInactiveFrameActive, kTopButtonIconAlpha);
  mixer[kColorTopButtonIconInactive] = ui::SetAlpha(
      kColorTabForegroundInactiveFrameActive, kTopButtonIconInactiveAlpha);
  mixer[kColorTopButtonIconLit] = {kColorTabForegroundInactiveFrameActive};
  mixer[kColorTopButtonFill] = {kColorNewTabButtonCRBackgroundFrameActive};
  if (!UsesTint(key)) {
    return;
  }
  const Tint tint = TintFor(key);
  const SkColor text = TextColorOn(tint);
  for (ui::ColorId foreground :
       {kColorTabForegroundActiveFrameActive,
        kColorTabForegroundActiveFrameInactive,
        kColorTabForegroundInactiveFrameActive,
        kColorTabForegroundInactiveFrameInactive,
        kColorNewTabButtonCRForegroundFrameActive,
        kColorNewTabButtonCRForegroundFrameInactive,
        kColorTabSearchButtonCRForegroundFrameActive,
        kColorTabSearchButtonCRForegroundFrameInactive}) {
    mixer[foreground] = {text};
  }
  // A site's own picture-in-picture window, such as a Meet call, wears the
  // tint in its bar, per docs/plans/mini-player.md.
  mixer[kColorPipWindowTopBarBackground] = {ui::kColorFrameActive};
  mixer[kColorPipWindowForeground] = {text};
  mixer[kColorPipWindowForegroundInactive] = {SkColorSetA(text, 0x8C)};
  mixer[kColorVerticalTabPinnedOutline] = {SK_ColorTRANSPARENT};
  mixer[kColorVerticalTabStripShadow] = {SK_ColorTRANSPARENT};
  const SkColor active_tab = ActiveTabOn(tint);
  mixer[kColorTabBackgroundActiveFrameActive] = {active_tab};
  mixer[kColorTabBackgroundActiveFrameInactive] = {active_tab};
  if (!IsDarkMode(key)) {
    return;
  }
  mixer[kColorToolbar] = {kBlackCard};
  mixer[kColorLocationBarBackground] = {kBlackCardField};
  mixer[kColorLocationBarBackgroundHovered] = {kBlackCardLine};
  mixer[kColorOmniboxResultsBackground] = {kBlackCardField};
  mixer[kColorOmniboxResultsBackgroundHovered] = {kBlackCardLine};
  mixer[kColorOmniboxResultsBackgroundSelected] = {kBlackCardEdge};
  mixer[kColorToolbarContentAreaSeparator] = {kBlackCardLine};
  mixer[kColorPageCardEdge] = {kBlackCardEdge};
}

}  // namespace uw
