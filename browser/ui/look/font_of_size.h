// Copyright 2026 The uw Authors. All rights reserved.

#ifndef UW_BROWSER_UI_LOOK_FONT_OF_SIZE_H_
#define UW_BROWSER_UI_LOOK_FONT_OF_SIZE_H_

#include "ui/gfx/font.h"
#include "ui/gfx/font_list.h"
#include "ui/views/controls/label.h"

namespace uw {

// `base` at `size` px.
inline gfx::FontList FontOfSize(const gfx::FontList& base, int size) {
  return base.DeriveWithSizeDelta(size - base.GetFontSize());
}

// The font of labels at `size` px.
inline gfx::FontList FontOfSize(
    int size,
    gfx::Font::Weight weight = gfx::Font::Weight::NORMAL) {
  return FontOfSize(views::Label::GetDefaultFontList(), size)
      .DeriveWithWeight(weight);
}

}  // namespace uw

#endif  // UW_BROWSER_UI_LOOK_FONT_OF_SIZE_H_
