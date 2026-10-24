// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/browser/ui/views/mini_player/mini_player_controls.h"

#include <algorithm>
#include <utility>

#include "cc/paint/paint_flags.h"
#include "cc/paint/paint_shader.h"
#include "chrome/grit/generated_resources.h"
#include "third_party/skia/include/core/SkPath.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/compositor/layer.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/font_list.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/gfx/geometry/rounded_corners_f.h"
#include "ui/gfx/geometry/skia_conversions.h"
#include "ui/gfx/paint_vector_icon.h"
#include "ui/gfx/scoped_canvas.h"
#include "ui/gfx/text_constants.h"
#include "ui/gfx/text_utils.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/controls/label.h"
#include "uw/browser/ui/vector_icons/vector_icons.h"

namespace uw {
namespace {

// The blur under a control at full glass, as in the frames.
constexpr float kGlassBlur = 10;

// An ad's progress bar.
constexpr SkColor kAdYellow = SkColorSetRGB(0xF7, 0xC9, 0x48);

SkColor White(float alpha) {
  return SkColorSetA(SK_ColorWHITE, std::clamp(alpha, 0.f, 1.f) * 255);
}

// The system font at `size` points.
gfx::FontList FontOfSize(int size, gfx::Font::Weight weight) {
  const gfx::FontList& base = views::Label::GetDefaultFontList();
  return base.DeriveWithSizeDelta(size - base.GetFontSize())
      .DeriveWithWeight(weight);
}

void PaintVerticalGradient(gfx::Canvas* canvas,
                           const SkPath& path,
                           const gfx::RectF& bounds,
                           SkColor top,
                           SkColor bottom,
                           float end = 1) {
  const SkPoint points[2] = {gfx::PointFToSkPoint(bounds.top_center()),
                             SkPoint::Make(bounds.CenterPoint().x(),
                                           bounds.y() + bounds.height() * end)};
  const SkColor4f colors[2] = {SkColor4f::FromColor(top),
                               SkColor4f::FromColor(bottom)};
  cc::PaintFlags flags;
  flags.setAntiAlias(true);
  flags.setShader(cc::PaintShader::MakeLinearGradient(points, colors, nullptr,
                                                      2, SkTileMode::kClamp));
  canvas->DrawPath(path, flags);
}

}  // namespace

void UpdateControlGlass(views::View* view, int glass, float radius) {
  if (!view->layer()) {
    view->SetPaintToLayer();
    view->layer()->SetFillsBoundsOpaquely(false);
  }
  view->layer()->SetRoundedCornerRadius(gfx::RoundedCornersF(radius));
  view->layer()->SetIsFastRoundedCorner(true);
  view->layer()->SetBackgroundBlur(kGlassBlur * glass / 100.f);
}

void PaintControlFill(gfx::Canvas* canvas,
                      const gfx::RectF& bounds,
                      float radius,
                      int glass,
                      std::optional<SkColor> color) {
  const SkPath outline =
      SkPath::RRect(gfx::RectFToSkRect(bounds), radius, radius);
  cc::PaintFlags flags;
  flags.setAntiAlias(true);
  if (color) {
    flags.setColor(*color);
    canvas->DrawPath(outline, flags);
    return;
  }
  const float clear = glass / 100.f;
  flags.setColor(SkColorSetA(SK_ColorBLACK, 0x66 * (1 - clear)));
  canvas->DrawPath(outline, flags);
  if (glass == 0) {
    return;
  }
  PaintVerticalGradient(canvas, outline, bounds, White(.24f * clear),
                        White(.07f * clear));
  gfx::RectF edge = bounds;
  edge.Inset(0.5f);
  const SkPath ring =
      SkPath::RRect(gfx::RectFToSkRect(edge), radius - 0.5f, radius - 0.5f);
  cc::PaintFlags line;
  line.setAntiAlias(true);
  line.setStyle(cc::PaintFlags::kStroke_Style);
  line.setStrokeWidth(1);
  line.setColor(White(.14f * clear));
  canvas->DrawPath(ring, line);
  // The lit top edge fades out a third of the way down.
  const SkPoint points[2] = {
      gfx::PointFToSkPoint(edge.top_center()),
      SkPoint::Make(edge.CenterPoint().x(), edge.y() + edge.height() / 3)};
  const SkColor4f colors[2] = {SkColor4f::FromColor(White(.5f * clear)),
                               SkColor4f::FromColor(White(0))};
  line.setShader(cc::PaintShader::MakeLinearGradient(points, colors, nullptr, 2,
                                                     SkTileMode::kClamp));
  canvas->DrawPath(ring, line);
}

MiniPlayerButton::MiniPlayerButton(PressedCallback callback,
                                   const gfx::VectorIcon& icon,
                                   std::u16string name,
                                   ControlsGlassOf glass)
    : views::Button(std::move(callback)),
      icon_(icon),
      glass_(std::move(glass)) {
  SetTooltipText(name);
  GetViewAccessibility().SetName(name);
  SetInstallFocusRingOnFocus(true);
}

MiniPlayerButton::~MiniPlayerButton() = default;

void MiniPlayerButton::SetIcon(const gfx::VectorIcon& icon,
                               std::u16string name) {
  icon_ = raw_ref(icon);
  SetTooltipText(name);
  GetViewAccessibility().SetName(name);
  SchedulePaint();
}

void MiniPlayerButton::SetSizes(int diameter, int icon_size) {
  diameter_ = diameter;
  icon_size_ = icon_size;
  SetPreferredSize(gfx::Size(diameter, diameter));
  UpdateGlass();
}

void MiniPlayerButton::UpdateGlass() {
  UpdateControlGlass(this, fill_ ? 0 : glass_.Run(), diameter_ / 2.f);
  SchedulePaint();
}

void MiniPlayerButton::PaintButtonContents(gfx::Canvas* canvas) {
  const gfx::RectF bounds(GetLocalBounds());
  PaintControlFill(canvas, bounds, bounds.width() / 2, glass_.Run(), fill_);
  if (GetState() == STATE_HOVERED || GetState() == STATE_PRESSED) {
    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    flags.setColor(White(GetState() == STATE_PRESSED ? .2f : .12f));
    canvas->DrawCircle(bounds.CenterPoint(), bounds.width() / 2, flags);
  }
  const gfx::ImageSkia image =
      gfx::CreateVectorIcon(*icon_, icon_size_, SK_ColorWHITE);
  canvas->DrawImageInt(image, (width() - image.width()) / 2,
                       (height() - image.height()) / 2);
  if (!seconds_.empty()) {
    canvas->DrawStringRectWithFlags(seconds_,
                                    FontOfSize(std::max(7, icon_size_ * 7 / 17),
                                               gfx::Font::Weight::SEMIBOLD),
                                    SK_ColorWHITE,
                                    GetLocalBounds() + gfx::Vector2d(0, 1),
                                    gfx::Canvas::TEXT_ALIGN_CENTER);
  }
}

BEGIN_METADATA(MiniPlayerButton)
END_METADATA

SkipAdButton::SkipAdButton(PressedCallback callback, ControlsGlassOf glass)
    : views::Button(std::move(callback)), glass_(std::move(glass)) {
  const std::u16string name =
      l10n_util::GetStringUTF16(IDS_PICTURE_IN_PICTURE_SKIP_AD_CONTROL_TEXT);
  SetTooltipText(name);
  GetViewAccessibility().SetName(name);
}

SkipAdButton::~SkipAdButton() = default;

void SkipAdButton::SetHeight(int height) {
  height_ = height;
  PreferredSizeChanged();
  UpdateGlass();
}

void SkipAdButton::UpdateGlass() {
  UpdateControlGlass(this, glass_.Run(), height_ / 2.f);
  SchedulePaint();
}

gfx::Size SkipAdButton::CalculatePreferredSize(
    const views::SizeBounds& available_size) const {
  const gfx::FontList font =
      FontOfSize(height_ / 2, gfx::Font::Weight::SEMIBOLD);
  const int text = gfx::GetStringWidth(
      l10n_util::GetStringUTF16(IDS_PICTURE_IN_PICTURE_SKIP_AD_CONTROL_TEXT) +
          u" ›",
      font);
  return gfx::Size(text + height_, height_);
}

void SkipAdButton::PaintButtonContents(gfx::Canvas* canvas) {
  const gfx::RectF bounds(GetLocalBounds());
  PaintControlFill(canvas, bounds, bounds.height() / 2, glass_.Run());
  cc::PaintFlags flags;
  flags.setAntiAlias(true);
  flags.setStyle(cc::PaintFlags::kStroke_Style);
  flags.setStrokeWidth(1);
  flags.setColor(White(GetState() == STATE_HOVERED ? .5f : .3f));
  gfx::RectF ring = bounds;
  ring.Inset(0.5f);
  canvas->DrawRoundRect(ring, ring.height() / 2, flags);
  canvas->DrawStringRectWithFlags(
      l10n_util::GetStringUTF16(IDS_PICTURE_IN_PICTURE_SKIP_AD_CONTROL_TEXT) +
          u" ›",
      FontOfSize(height_ / 2, gfx::Font::Weight::SEMIBOLD), SK_ColorWHITE,
      GetLocalBounds(), gfx::Canvas::TEXT_ALIGN_CENTER);
}

BEGIN_METADATA(SkipAdButton)
END_METADATA

FastLabel::FastLabel(ControlsGlassOf glass) : glass_(std::move(glass)) {
  GetViewAccessibility().SetRole(ax::mojom::Role::kStaticText);
  GetViewAccessibility().SetName(u"2×");
}

FastLabel::~FastLabel() = default;

void FastLabel::SetHeight(int height) {
  height_ = height;
  PreferredSizeChanged();
  UpdateGlass();
}

void FastLabel::UpdateGlass() {
  UpdateControlGlass(this, glass_.Run(), height_ / 2.f);
  SchedulePaint();
}

gfx::Size FastLabel::CalculatePreferredSize(
    const views::SizeBounds& available_size) const {
  const int text = gfx::GetStringWidth(
      u"2×", FontOfSize(height_ / 2, gfx::Font::Weight::SEMIBOLD));
  return gfx::Size(height_ / 3 + height_ / 2 + 4 + text + height_ * 5 / 12,
                   height_);
}

void FastLabel::OnPaint(gfx::Canvas* canvas) {
  const gfx::RectF bounds(GetLocalBounds());
  const int glass = glass_.Run();
  const SkPath outline = SkPath::RRect(
      gfx::RectFToSkRect(bounds), bounds.height() / 2, bounds.height() / 2);
  if (glass == 0) {
    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    flags.setColor(SkColorSetA(SK_ColorBLACK, 0x73));
    canvas->DrawPath(outline, flags);
  } else {
    PaintControlFill(canvas, bounds, bounds.height() / 2, glass);
  }
  const int icon_size = height_ / 2 + 1;
  const int left = height_ / 3;
  canvas->DrawImageInt(gfx::CreateVectorIcon(kMiniPlayerFastForwardIcon,
                                             icon_size, SK_ColorWHITE),
                       left, (height_ - icon_size) / 2);
  gfx::Rect text = GetLocalBounds();
  text.Inset(gfx::Insets::TLBR(0, left + icon_size + 4, 0, 0));
  canvas->DrawStringRectWithFlags(
      u"2×", FontOfSize(height_ / 2, gfx::Font::Weight::SEMIBOLD),
      SK_ColorWHITE, text, gfx::Canvas::TEXT_ALIGN_LEFT);
}

BEGIN_METADATA(FastLabel)
END_METADATA

MiniPlayerProgress::MiniPlayerProgress(SeekCallback seek)
    : seek_(std::move(seek)) {}

MiniPlayerProgress::~MiniPlayerProgress() = default;

void MiniPlayerProgress::SetProgress(double progress) {
  if (dragging_) {
    return;
  }
  progress_ = std::clamp(progress, 0.0, 1.0);
  SchedulePaint();
}

void MiniPlayerProgress::SetSizes(int bar_height, int knob) {
  bar_height_ = bar_height;
  knob_ = knob;
  SchedulePaint();
}

void MiniPlayerProgress::SetSeekable(bool seekable) {
  seekable_ = seekable;
  SchedulePaint();
}

void MiniPlayerProgress::SetAd(bool ad) {
  ad_ = ad;
  SchedulePaint();
}

void MiniPlayerProgress::OnPaint(gfx::Canvas* canvas) {
  const float center = height() / 2.f;
  const gfx::RectF track(0, center - bar_height_ / 2.f, width(), bar_height_);
  cc::PaintFlags flags;
  flags.setAntiAlias(true);
  flags.setColor(White(.32f));
  canvas->DrawRoundRect(track, bar_height_ / 2.f, flags);
  gfx::RectF played = track;
  played.set_width(track.width() * progress_);
  flags.setColor(ad_ ? kAdYellow : SK_ColorWHITE);
  canvas->DrawRoundRect(played, bar_height_ / 2.f, flags);
  if (!seekable_ || ad_) {
    return;
  }
  cc::PaintFlags shadow;
  shadow.setAntiAlias(true);
  shadow.setColor(SkColorSetA(SK_ColorBLACK, 0x59));
  const gfx::PointF knob(played.right(), center);
  canvas->DrawCircle(knob + gfx::Vector2dF(0, 1), knob_ / 2.f + 0.5f, shadow);
  flags.setColor(SK_ColorWHITE);
  canvas->DrawCircle(knob, knob_ / 2.f, flags);
}

double MiniPlayerProgress::ProgressAt(int x) const {
  return width() > 0 ? std::clamp(static_cast<double>(x) / width(), 0.0, 1.0)
                     : 0.0;
}

bool MiniPlayerProgress::OnMousePressed(const ui::MouseEvent& event) {
  if (!seekable_ || ad_ || !event.IsOnlyLeftMouseButton()) {
    return false;
  }
  dragging_ = true;
  progress_ = ProgressAt(event.x());
  SchedulePaint();
  return true;
}

bool MiniPlayerProgress::OnMouseDragged(const ui::MouseEvent& event) {
  if (!dragging_) {
    return false;
  }
  progress_ = ProgressAt(event.x());
  SchedulePaint();
  return true;
}

void MiniPlayerProgress::OnMouseReleased(const ui::MouseEvent& event) {
  if (!dragging_) {
    return;
  }
  dragging_ = false;
  progress_ = ProgressAt(event.x());
  seek_.Run(progress_);
  SchedulePaint();
}

void MiniPlayerProgress::OnMouseCaptureLost() {
  dragging_ = false;
}

BEGIN_METADATA(MiniPlayerProgress)
END_METADATA

ControlsScrim::ControlsScrim() {
  SetCanProcessEventsWithinSubtree(false);
}

ControlsScrim::~ControlsScrim() = default;

void ControlsScrim::OnPaint(gfx::Canvas* canvas) {
  const gfx::RectF bounds(GetLocalBounds());
  const SkPoint points[2] = {gfx::PointFToSkPoint(bounds.top_center()),
                             gfx::PointFToSkPoint(bounds.bottom_center())};
  const SkColor4f colors[4] = {
      SkColor4f::FromColor(SkColorSetA(SK_ColorBLACK, 0x80)),
      SkColor4f::FromColor(SkColorSetA(SK_ColorBLACK, 0x33)),
      SkColor4f::FromColor(SkColorSetA(SK_ColorBLACK, 0x33)),
      SkColor4f::FromColor(SkColorSetA(SK_ColorBLACK, 0xA3))};
  const SkScalar positions[4] = {0, .32f, .58f, 1};
  cc::PaintFlags flags;
  flags.setShader(cc::PaintShader::MakeLinearGradient(points, colors, positions,
                                                      4, SkTileMode::kClamp));
  canvas->DrawRect(bounds, flags);
}

BEGIN_METADATA(ControlsScrim)
END_METADATA

Rim::Rim(float radius, std::optional<SkColor> ring)
    : radius_(radius), ring_(ring) {
  SetCanProcessEventsWithinSubtree(false);
  SetPaintToLayer();
  layer()->SetFillsBoundsOpaquely(false);
}

Rim::~Rim() = default;

void Rim::OnPaint(gfx::Canvas* canvas) {
  gfx::RectF edge(GetLocalBounds());
  edge.Inset(0.5f);
  if (ring_) {
    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    flags.setStyle(cc::PaintFlags::kStroke_Style);
    flags.setStrokeWidth(1);
    flags.setColor(*ring_);
    canvas->DrawRoundRect(edge, radius_ - 0.5f, flags);
    return;
  }
  const SkPoint points[2] = {gfx::PointFToSkPoint(edge.origin()),
                             gfx::PointFToSkPoint(edge.bottom_right())};
  const SkColor4f colors[4] = {
      SkColor4f::FromColor(White(.7f)), SkColor4f::FromColor(White(.14f)),
      SkColor4f::FromColor(White(.04f)), SkColor4f::FromColor(White(.32f))};
  const SkScalar positions[4] = {0, .22f, .5f, 1};
  cc::PaintFlags flags;
  flags.setAntiAlias(true);
  flags.setStyle(cc::PaintFlags::kStroke_Style);
  flags.setStrokeWidth(1);
  flags.setShader(cc::PaintShader::MakeLinearGradient(points, colors, positions,
                                                      4, SkTileMode::kClamp));
  canvas->DrawRoundRect(edge, radius_ - 0.5f, flags);
}

BEGIN_METADATA(Rim)
END_METADATA

}  // namespace uw
