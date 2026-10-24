// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/browser/ui/look/sidebar_tab_fill.h"

#include <algorithm>
#include <cmath>

#include "base/numerics/safe_conversions.h"
#include "cc/paint/paint_flags.h"
#include "cc/paint/paint_shader.h"
#include "chrome/browser/ui/color/chrome_color_id.h"
#include "third_party/skia/include/core/SkPath.h"
#include "ui/color/color_id.h"
#include "ui/color/color_provider.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/color_utils.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/views/view.h"
#include "uw/components/look/theme.h"

namespace uw {
namespace {

// White at these strengths, on a light theme and on a dark one.
struct Whites {
  float light;
  float dark;
};

// A tile fades from its top to its bottom, and brightens under the pointer.
constexpr Whites kTileTop = {0.50f, 0.13f};
constexpr Whites kTileBottom = {0.20f, 0.05f};
constexpr Whites kHoveredTileTop = {0.68f, 0.20f};
constexpr Whites kHoveredTileBottom = {0.36f, 0.09f};
constexpr Whites kActiveFill = {0.86f, 0.14f};
// The active tab stays this white at the clearest, so it still stands out.
constexpr Whites kLeastActiveFill = {0.40f, 0.10f};
constexpr Whites kTopEdge = {0.90f, 0.20f};
constexpr Whites kActiveTopEdge = {1.0f, 0.22f};
constexpr Whites kRing = {0.30f, 0.07f};

SkColor WhiteAt(double strength) {
  return SkColorSetA(SK_ColorWHITE,
                     base::ClampRound<SkAlpha>(strength * SK_AlphaOPAQUE));
}

SkColor White(Whites whites, bool dark) {
  return WhiteAt(dark ? whites.dark : whites.light);
}

// The fill of a glass tile, from `from` toward `to` by `amount`, keeping
// `tint` of its white.
SkColor Between(Whites from, Whites to, double amount, bool dark, float tint) {
  return WhiteAt(tint * std::lerp(dark ? from.dark : from.light,
                                  dark ? to.dark : to.light, amount));
}

}  // namespace

std::optional<SkColor> SidebarTabFill(
    const views::View* tab,
    bool pinned,
    TabStyle::TabSelectionState selection_state,
    double hover) {
  const ui::ColorProvider* colors = tab->GetColorProvider();
  SkColor fill = SK_ColorTRANSPARENT;
  switch (selection_state) {
    case TabStyle::TabSelectionState::kActive:
      return std::nullopt;
    case TabStyle::TabSelectionState::kSelected:
      fill = colors->GetColor(ui::kColorSysStateHeaderSelect);
      break;
    case TabStyle::TabSelectionState::kInactive:
      if (pinned) {
        fill = colors->GetColor(kColorNewTabButtonCRBackgroundFrameActive);
      }
      break;
  }
  const SkColor hovered =
      colors->GetColor(kColorTabBackgroundInactiveHoverFrameActive);
  return color_utils::GetResultingPaintColor(
      SkColorSetA(hovered,
                  base::ClampRound<SkAlpha>(SkColorGetA(hovered) * hover)),
      fill);
}

void PaintGlassTabFill(gfx::Canvas* canvas,
                       const views::View* tab,
                       const SkPath& shape,
                       const gfx::Rect& bounds,
                       float scale,
                       bool active,
                       double hover,
                       int glass) {
  const float tint = GlassTint(glass);
  const bool dark = color_utils::IsDark(
      tab->GetColorProvider()->GetColor(ui::kColorFrameActive));
  cc::PaintFlags fill;
  fill.setAntiAlias(true);
  if (active) {
    fill.setColor(WhiteAt(
        std::max(tint * (dark ? kActiveFill.dark : kActiveFill.light),
                 dark ? kLeastActiveFill.dark : kLeastActiveFill.light)));
  } else {
    const SkPoint ends[] = {SkPoint::Make(0, bounds.y()),
                            SkPoint::Make(0, bounds.bottom())};
    const SkColor4f colors[] = {
        SkColor4f::FromColor(
            Between(kTileTop, kHoveredTileTop, hover, dark, tint)),
        SkColor4f::FromColor(
            Between(kTileBottom, kHoveredTileBottom, hover, dark, tint))};
    fill.setShader(cc::PaintShader::MakeLinearGradient(ends, colors, nullptr, 2,
                                                       SkTileMode::kClamp));
  }
  canvas->DrawRect(bounds, fill);
  PaintGlassEdge(canvas, tab, shape, bounds, scale, active);
}

void PaintGlassEdge(gfx::Canvas* canvas,
                    const views::View* view,
                    const SkPath& shape,
                    const gfx::Rect& bounds,
                    float scale,
                    bool bright) {
  const bool dark = color_utils::IsDark(
      view->GetColorProvider()->GetColor(ui::kColorFrameActive));
  cc::PaintFlags edge;
  edge.setAntiAlias(true);
  edge.setColor(White(bright ? kActiveTopEdge : kTopEdge, dark));
  canvas->DrawRect(gfx::RectF(bounds.x(), bounds.y(), bounds.width(), scale),
                   edge);

  cc::PaintFlags ring;
  ring.setAntiAlias(true);
  ring.setStyle(cc::PaintFlags::kStroke_Style);
  ring.setStrokeWidth(2 * scale);
  ring.setColor(White(kRing, dark));
  canvas->DrawPath(shape, ring);
}

}  // namespace uw
