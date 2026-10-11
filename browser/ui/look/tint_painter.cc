// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/browser/ui/look/tint_painter.h"

#include <cmath>
#include <numbers>

#include "cc/paint/paint_flags.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/point.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/skia_paint_util.h"
#include "ui/views/view.h"
#include "uw/components/look/tint.h"

namespace uw {
namespace {

// The gradient runs mostly downward and a little toward the trailing edge,
// like `linear-gradient(160deg, ...)` in the designs.
constexpr double kAngle = 160.0 * std::numbers::pi / 180.0;

}  // namespace

void PaintTint(gfx::Canvas* canvas,
               const views::View* view,
               const views::View* window,
               SkColor color) {
  const Tint tint = TintFrom(color);
  const double across = std::sin(kAngle);
  const double down = -std::cos(kAngle);
  const double length =
      std::abs(window->width() * across) + std::abs(window->height() * down);
  gfx::Point origin;
  views::View::ConvertPointToTarget(window, view, &origin);
  const double middle_x = origin.x() + window->width() / 2.0;
  const double middle_y = origin.y() + window->height() / 2.0;
  cc::PaintFlags flags;
  flags.setShader(gfx::CreateGradientShader(
      gfx::Point(std::lround(middle_x - across * length / 2),
                 std::lround(middle_y - down * length / 2)),
      gfx::Point(std::lround(middle_x + across * length / 2),
                 std::lround(middle_y + down * length / 2)),
      tint.start, tint.end));
  canvas->DrawRect(view->GetLocalBounds(), flags);
}

}  // namespace uw
