// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/browser/ui/look/tint_color_mixer.h"

#include "chrome/browser/ui/color/chrome_color_id.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/color/color_id.h"
#include "ui/color/color_mixer.h"
#include "ui/color/color_provider.h"
#include "ui/color/color_provider_key.h"
#include "ui/color/color_recipe.h"
#include "ui/color/color_transform.h"
#include "ui/gfx/color_utils.h"
#include "uw/components/look/tint.h"

namespace uw {
namespace {

constexpr SkAlpha kDividerAlpha = 0x1A;
constexpr SkAlpha kHoverOnLightAlpha = 0x75;
constexpr SkAlpha kHoverOnDarkAlpha = 0x14;

// Custom themes bring their own frame and tab colors.
bool UsesTint(const ui::ColorProviderKey& key) {
  return !key.custom_theme;
}

// The color the user picked in Chromium's theme color picker, when there is
// one. Otherwise the default for light or dark mode.
Tint TintFor(const ui::ColorProviderKey& key) {
  if (key.user_color &&
      key.user_color_source == ui::ColorProviderKey::UserColorSource::kAccent) {
    return TintFrom(*key.user_color);
  }
  return TintFrom(key.color_mode == ui::ColorProviderKey::ColorMode::kDark
                      ? kDefaultDarkTint
                      : kDefaultLightTint);
}

}  // namespace

void AddTintColorMixer(ui::ColorProvider* provider,
                       const ui::ColorProviderKey& key) {
  if (!UsesTint(key)) {
    return;
  }
  const Tint tint = TintFor(key);
  const SkColor text = TextColorOn(tint);
  const SkColor hover = SkColorSetA(
      SK_ColorWHITE, IsDark(tint) ? kHoverOnDarkAlpha : kHoverOnLightAlpha);
  ui::ColorMixer& mixer = provider->AddMixer();
  for (ui::ColorId frame :
       {ui::kColorSysHeader, ui::kColorSysHeaderInactive, ui::kColorFrameActive,
        ui::kColorFrameActiveUnthemed, ui::kColorFrameInactive}) {
    mixer[frame] = {tint.start};
  }
  mixer[ui::kColorSysHeaderContainer] = {FillOn(tint)};
  mixer[ui::kColorSysHeaderContainerInactive] = {FillOn(tint)};
  mixer[ui::kColorSysOnHeaderPrimary] = {text};
  mixer[ui::kColorSysOnHeaderPrimaryInactive] = {text};
  mixer[ui::kColorSysOnHeaderDivider] = {SkColorSetA(text, kDividerAlpha)};
  mixer[ui::kColorSysOnHeaderDividerInactive] = {
      SkColorSetA(text, kDividerAlpha)};
  mixer[ui::kColorSysStateHeaderHover] = {hover};
  mixer[ui::kColorSysStateHeaderHoverInactive] = {hover};
  mixer[ui::kColorSysStateOnHeaderHover] = {hover};
}

void AddTintTabColorMixer(ui::ColorProvider* provider,
                          const ui::ColorProviderKey& key) {
  if (!UsesTint(key)) {
    return;
  }
  const Tint tint = TintFor(key);
  const SkColor text = TextColorOn(tint);
  ui::ColorMixer& mixer = provider->AddMixer();
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
  mixer[kColorVerticalTabPinnedOutline] = {SK_ColorTRANSPARENT};
  mixer[kColorVerticalTabStripShadow] = {SK_ColorTRANSPARENT};
  mixer[kColorTabBackgroundActiveFrameActive] = {ActiveTabOn(tint)};
  mixer[kColorTabBackgroundActiveFrameInactive] = {ActiveTabOn(tint)};
}

}  // namespace uw
