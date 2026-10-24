// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/browser/ui/glass/glass_background.h"

#include <utility>

#include "base/functional/bind.h"
#include "cc/paint/paint_flags.h"
#include "third_party/skia/include/core/SkPath.h"
#include "ui/color/color_provider.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/rect_conversions.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/gfx/geometry/skia_conversions.h"
#include "ui/gfx/scoped_canvas.h"
#include "ui/views/background.h"
#include "ui/views/view.h"
#include "uw/browser/ui/glass/glass.h"
#include "uw/browser/ui/look/sidebar_tab_fill.h"

namespace uw {
namespace {

// A card over glass keeps this much of its color, as menu cards do.
constexpr SkAlpha kCardOverGlass = 0xD9;

enum class Shape { kPill, kCard };

class GlassBackground : public views::Background {
 public:
  GlassBackground(GlassOf glass, ui::ColorId color, float radius, Shape shape)
      : glass_(std::move(glass)),
        color_(color),
        radius_(radius),
        shape_(shape) {}

  // views::Background:
  void Paint(gfx::Canvas* canvas, views::View* view) const override {
    const SkPath outline = SkPath::RRect(
        gfx::RectToSkRect(view->GetLocalBounds()), radius_, radius_);
    const SkColor color = view->GetColorProvider()->GetColor(color_);
    const int glass = glass_.Run();
    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    if (glass == 0) {
      flags.setColor(color);
      canvas->DrawPath(outline, flags);
      return;
    }
    if (shape_ == Shape::kPill) {
      PaintGlassPill(canvas, view, gfx::RectF(view->GetLocalBounds()), radius_,
                     glass, /*hover=*/0);
      return;
    }
    gfx::ScopedCanvas scoped_canvas(canvas);
    canvas->ClipPath(outline, true);
    flags.setColor(SkColorSetA(color, kCardOverGlass));
    canvas->DrawPath(outline, flags);
    PaintGlassEdge(canvas, view, outline, view->GetLocalBounds(), 1.0f);
  }

 private:
  const GlassOf glass_;
  const ui::ColorId color_;
  const float radius_;
  const Shape shape_;
};

}  // namespace

GlassOf GlassOfWindow(BrowserWindowInterface* window) {
  return base::BindRepeating(&ElementGlassOfWindow, base::Unretained(window));
}

std::unique_ptr<views::Background> CreateGlassPillBackground(GlassOf glass,
                                                             ui::ColorId color,
                                                             float radius) {
  return std::make_unique<GlassBackground>(std::move(glass), color, radius,
                                           Shape::kPill);
}

std::unique_ptr<views::Background> CreateGlassCardBackground(GlassOf glass,
                                                             ui::ColorId color,
                                                             float radius) {
  return std::make_unique<GlassBackground>(std::move(glass), color, radius,
                                           Shape::kCard);
}

void PaintGlassPill(gfx::Canvas* canvas,
                    const views::View* view,
                    const gfx::RectF& bounds,
                    float radius,
                    int glass,
                    double hover) {
  const SkPath outline =
      SkPath::RRect(gfx::RectFToSkRect(bounds), radius, radius);
  gfx::ScopedCanvas scoped_canvas(canvas);
  canvas->ClipPath(outline, true);
  PaintGlassTabFill(canvas, view, outline, gfx::ToEnclosingRect(bounds), 1.0f,
                    /*active=*/false, hover, glass);
}

}  // namespace uw
