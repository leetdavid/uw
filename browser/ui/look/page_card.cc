// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/browser/ui/look/page_card.h"

#include "build/build_config.h"
#include "components/prefs/pref_service.h"
#include "uw/browser/ui/look/page_space_prefs.h"

#if BUILDFLAG(IS_MAC)
#include "base/mac/mac_util.h"
#endif

namespace uw {

gfx::Insets PageCardMargins(const PrefService* local_state) {
  return !local_state || local_state->GetBoolean(kPageSpacePref)
             ? kPageCardMargins
             : gfx::Insets();
}

gfx::Insets LittleWindowMargins(const PrefService* local_state) {
  return PageCardMargins(local_state).IsEmpty() ? gfx::Insets()
                                                : kLittleWindowMargins;
}

int LittleWindowToolbarStart(const PrefService* local_state) {
#if BUILDFLAG(IS_MAC)
  // Where macOS's window buttons end, as BrowserFrameViewMac places them,
  // and a gap before the back button.
  constexpr int kGap = 10;
  const int buttons_end =
      base::mac::MacOSMajorVersion() >= 26 ? 12 + 62 : 20 + 54;
  return buttons_end + kGap - LittleWindowMargins(local_state).left();
#else
  return 0;
#endif
}

gfx::RoundedCornersF PageCardCorners(const gfx::RectF& card,
                                     const gfx::RectF& window) {
  const float left = card.x() <= window.x() ? 0 : kPageCardRadius;
  const float right = card.right() >= window.right() ? 0 : kPageCardRadius;
  return gfx::RoundedCornersF(left, right, right, left);
}

}  // namespace uw
