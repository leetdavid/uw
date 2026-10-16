// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/browser/ui/look/tint_painter.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <vector>

#include "base/no_destructor.h"
#include "build/build_config.h"
#include "cc/paint/paint_flags.h"
#include "cc/paint/paint_image.h"
#include "cc/paint/paint_shader.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "third_party/skia/include/core/SkMatrix.h"
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

// Full grain is gray noise around middle gray, as in the designs, multiplied
// over a light tint at 35%, one noise dot per screen pixel. A dark tint has no
// room to get darker, so the grain is screened over it instead.
constexpr float kFullGrainOpacity = 0.35f;
constexpr int kGrainTileSize = 128;
constexpr uint8_t kGrainDarkest = 90;
constexpr uint8_t kGrainLightest = 166;

// A tile of gray noise. Noise has no pattern, so the tiles meet without seams.
const cc::PaintImage& GrainTile() {
  static const base::NoDestructor<cc::PaintImage> tile([] {
    SkBitmap bitmap;
    bitmap.allocN32Pixels(kGrainTileSize, kGrainTileSize);
    uint32_t state = 0x9E3779B9;
    for (int y = 0; y < kGrainTileSize; ++y) {
      for (int x = 0; x < kGrainTileSize; ++x) {
        state = state * 1664525 + 1013904223;
        const uint8_t gray =
            kGrainDarkest +
            (state >> 24) % (kGrainLightest - kGrainDarkest + 1);
        *bitmap.getAddr32(x, y) = SkPreMultiplyARGB(0xFF, gray, gray, gray);
      }
    }
    bitmap.setImmutable();
    return cc::PaintImage::CreateFromBitmap(std::move(bitmap));
  }());
  return *tile;
}

// Draws grain over `view` at `grain` percent of full strength.
void PaintGrain(gfx::Canvas* canvas,
                const views::View* view,
                int grain,
                bool dark) {
  if (grain <= 0) {
    return;
  }
  const SkMatrix pixels =
      SkMatrix::Scale(1 / canvas->image_scale(), 1 / canvas->image_scale());
  cc::PaintFlags flags;
  flags.setShader(cc::PaintShader::MakeImage(GrainTile(), SkTileMode::kRepeat,
                                             SkTileMode::kRepeat, &pixels));
  flags.setBlendMode(dark ? SkBlendMode::kScreen : SkBlendMode::kMultiply);
  flags.setAlphaf(kFullGrainOpacity * std::min(grain, kFullGrain) / kFullGrain);
  canvas->DrawRect(view->GetLocalBounds(), flags);
}

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
  PaintGrain(canvas, view, ReadTheme(prefs, std::nullopt).grain, IsDark(tint));
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
