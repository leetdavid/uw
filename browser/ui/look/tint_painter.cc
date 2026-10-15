// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/browser/ui/look/tint_painter.h"

#include <cmath>
#include <numbers>
#include <vector>

#include "build/build_config.h"
#include "cc/paint/paint_flags.h"
#include "cc/paint/paint_shader.h"
#include "ui/color/color_provider.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/point.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/views/view.h"
#include "ui/views/widget/widget.h"
#include "uw/browser/ui/look/page_card.h"
#include "uw/components/look/theme_prefs.h"
#include "uw/components/look/tint.h"

#if BUILDFLAG(IS_MAC)
#include "uw/browser/ui/look/window_buttons.h"
#endif

namespace uw {
namespace {

// The gradient runs mostly downward and a little toward the trailing edge,
// like `linear-gradient(160deg, ...)` in the designs.
constexpr double kAngle = 160.0 * std::numbers::pi / 180.0;

}  // namespace

void PaintTint(gfx::Canvas* canvas,
               const views::View* view,
               const views::View* window,
               SkColor color,
               const PrefService& prefs) {
  const Tint tint = TintFrom(color);
  const double across = std::sin(kAngle);
  const double down = -std::cos(kAngle);
  const double length =
      std::abs(window->width() * across) + std::abs(window->height() * down);
  gfx::Point origin;
  views::View::ConvertPointToTarget(window, view, &origin);
  const double middle_x = origin.x() + window->width() / 2.0;
  const double middle_y = origin.y() + window->height() / 2.0;
  const SkPoint ends[] = {
      SkPoint::Make(static_cast<float>(middle_x - across * length / 2),
                    static_cast<float>(middle_y - down * length / 2)),
      SkPoint::Make(static_cast<float>(middle_x + across * length / 2),
                    static_cast<float>(middle_y + down * length / 2))};
  std::vector<SkColor> colors = GradientColorsFor(prefs, tint.start);
  if (colors.empty()) {
    colors = {tint.start, tint.end};
  }
  std::vector<SkColor4f> stops;
  std::vector<SkScalar> positions;
  for (size_t i = 0; i < colors.size(); ++i) {
    stops.push_back(SkColor4f::FromColor(colors[i]));
    positions.push_back(static_cast<SkScalar>(i) /
                        static_cast<SkScalar>(colors.size() - 1));
  }
  cc::PaintFlags flags;
  flags.setShader(cc::PaintShader::MakeLinearGradient(
      ends, stops.data(), positions.data(), static_cast<int>(stops.size()),
      SkTileMode::kClamp));
  canvas->DrawRect(view->GetLocalBounds(), flags);
#if BUILDFLAG(IS_MAC)
  // A dark tint already shows the inactive window buttons.
  if (TextColorOn(tint) == kDarkTextOnTint) {
    PaintWindowButtonDots(canvas, view, kDarkTextOnTint);
  }
#endif
}

void PaintPageCardEdge(gfx::Canvas* canvas,
                       const views::View* view,
                       const views::View* toolbar,
                       const views::View* page) {
  const SkColor color = view->GetColorProvider()->GetColor(kColorPageCardEdge);
  if (SkColorGetA(color) == SK_AlphaTRANSPARENT ||
      view->GetWidget()->IsFullscreen()) {
    return;
  }
  gfx::RectF edge = views::View::ConvertRectToTarget(
      toolbar, view, gfx::RectF(toolbar->GetLocalBounds()));
  edge.Union(views::View::ConvertRectToTarget(
      page, view, gfx::RectF(page->GetLocalBounds())));
  edge.Outset(0.5f);
  cc::PaintFlags flags;
  flags.setAntiAlias(true);
  flags.setStyle(cc::PaintFlags::kStroke_Style);
  flags.setStrokeWidth(1);
  flags.setColor(color);
  canvas->DrawRoundRect(edge, kPageCardRadius + 0.5f, flags);
}

}  // namespace uw
