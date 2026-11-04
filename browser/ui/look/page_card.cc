// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/browser/ui/look/page_card.h"

#include "components/prefs/pref_service.h"
#include "uw/browser/ui/look/page_space_prefs.h"

namespace uw {

gfx::Insets PageCardMargins(const PrefService* local_state) {
  return !local_state || local_state->GetBoolean(kPageSpacePref)
             ? kPageCardMargins
             : gfx::Insets();
}

gfx::RoundedCornersF PageCardCorners(const gfx::RectF& card,
                                     const gfx::RectF& window) {
  const float left = card.x() <= window.x() ? 0 : kPageCardRadius;
  const float right = card.right() >= window.right() ? 0 : kPageCardRadius;
  return gfx::RoundedCornersF(left, right, right, left);
}

}  // namespace uw
