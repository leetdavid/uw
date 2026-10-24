// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/browser/ui/look/tint_painter.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <numbers>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "base/no_destructor.h"
#include "build/build_config.h"
#include "cc/paint/paint_flags.h"
#include "cc/paint/paint_image.h"
#include "cc/paint/paint_shader.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "third_party/skia/include/core/SkMatrix.h"
#include "third_party/skia/include/core/SkRRect.h"
#include "ui/color/color_provider.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/point.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/gfx/geometry/skia_conversions.h"
#include "ui/gfx/scoped_canvas.h"
#include "ui/views/view.h"
#include "ui/views/widget/widget.h"
#include "uw/browser/ui/look/page_card.h"
#include "uw/components/look/theme.h"
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

struct Blend {
  SkColor color;
  Theme theme;
  float cover;
  float amount;
};

std::map<const views::View*, Blend>& Blends() {
  static base::NoDestructor<std::map<const views::View*, Blend>> blends;
  return *blends;
}

SkAlpha AlphaOf(float amount) {
  return static_cast<SkAlpha>(
      std::lround(std::clamp(amount, 0.0f, 1.0f) * 255));
}

// Where the page card is laid out in `view`, leaving out any transform on the
// toolbar or the page themselves, such as the page's nudge as a workspace
// switch fades in. A view drawn mid-nudge keeps its drawing after the nudge
// ends.
gfx::RectF PageCardIn(const views::View* view,
                      const views::View* toolbar,
                      const views::View* page) {
  const auto laid_out = [view](const views::View* part) {
    return views::View::ConvertRectToTarget(part->parent(), view,
                                            gfx::RectF(part->bounds()));
  };
  gfx::RectF card = laid_out(toolbar);
  card.Union(laid_out(page));
  return card;
}

void PaintSheet(gfx::Canvas* canvas,
                const views::View* view,
                const views::View* window,
                SkColor color,
                const Theme& theme,
                float cover) {
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
  std::vector<SkColor> colors = GradientColorsFor(theme, tint.start);
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
  {
    gfx::ScopedCanvas scoped_canvas(canvas);
    if (cover < 1.0f) {
      canvas->SaveLayerAlpha(AlphaOf(cover));
    }
    canvas->DrawRect(view->GetLocalBounds(), flags);
    PaintGrain(canvas, view, theme.grain, IsDark(tint));
  }
#if BUILDFLAG(IS_MAC)
  // A dark tint already shows the inactive window buttons.
  if (TextColorOn(tint) == kDarkTextOnTint) {
    PaintWindowButtonDots(canvas, view, kDarkTextOnTint);
  }
#endif
}

}  // namespace

void PaintTint(gfx::Canvas* canvas,
               const views::View* view,
               const views::View* window,
               SkColor color,
               const Theme& theme,
               float cover) {
  const auto blend = Blends().find(window);
  if (blend == Blends().end()) {
    PaintSheet(canvas, view, window, color, theme, cover);
    return;
  }
  // Each sheet in proportion and added together, so a fade between sheets over
  // glass lets through as much as the sheets do.
  const float amount = blend->second.amount;
  canvas->SaveLayerAlpha(AlphaOf(1.0f - amount));
  PaintSheet(canvas, view, window, color, theme, cover);
  canvas->Restore();
  cc::PaintFlags added;
  added.setBlendMode(SkBlendMode::kPlus);
  added.setAlphaf(amount);
  canvas->SaveLayerWithFlags(added);
  PaintSheet(canvas, view, window, blend->second.color, blend->second.theme,
             blend->second.cover);
  canvas->Restore();
}

void SetTintBlend(const views::View* window,
                  SkColor color,
                  const Theme& theme,
                  float cover,
                  float amount) {
  if (amount <= 0) {
    Blends().erase(window);
    return;
  }
  Blends()[window] = {color, theme, cover, std::min(amount, 1.0f)};
}

void ClipOutPageCard(gfx::Canvas* canvas,
                     const views::View* view,
                     const views::View* toolbar,
                     const views::View* page) {
  canvas->sk_canvas()->clipRRect(
      SkRRect::MakeRectXY(gfx::RectFToSkRect(PageCardIn(view, toolbar, page)),
                          kPageCardRadius, kPageCardRadius),
      SkClipOp::kDifference, /*do_anti_alias=*/true);
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
  gfx::RectF edge = PageCardIn(view, toolbar, page);
  edge.Outset(0.5f);
  cc::PaintFlags flags;
  flags.setAntiAlias(true);
  flags.setStyle(cc::PaintFlags::kStroke_Style);
  flags.setStrokeWidth(1);
  flags.setColor(color);
  canvas->DrawRoundRect(edge, kPageCardRadius + 0.5f, flags);
}

}  // namespace uw
