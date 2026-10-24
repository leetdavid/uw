// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/browser/ui/views/tab_organization/floating_window_buttons.h"

#include <memory>

#include "base/functional/bind.h"
#include "cc/paint/paint_flags.h"
#include "chrome/browser/ui/color/chrome_color_id.h"
#include "chrome/grit/generated_resources.h"
#include "third_party/skia/include/core/SkPath.h"
#include "third_party/skia/include/core/SkPathBuilder.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/color/color_provider.h"
#include "ui/events/event.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/point_f.h"
#include "ui/strings/grit/ui_strings.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/widget/widget.h"
#include "uw/browser/ui/look/window_buttons.h"

namespace uw {
namespace {

// macOS's size and spacing of its window buttons.
constexpr int kDiameter = 14;
constexpr int kGap = 10;
// A window that is not in front: the tint's text color at 24%.
constexpr SkAlpha kFaintAlpha = 0x3D;
constexpr SkAlpha kPressedAlpha = 0x2E;

struct Look {
  SkColor fill;
  SkColor rim;
  SkColor glyph;
  int name;
};

Look LookOf(WindowButton button) {
  switch (button) {
    case WindowButton::kClose:
      return {SkColorSetRGB(0xFF, 0x5F, 0x57), SkColorSetRGB(0xE0, 0x44, 0x3E),
              SkColorSetRGB(0x4D, 0x00, 0x00), IDS_APP_ACCNAME_CLOSE};
    case WindowButton::kMinimize:
      return {SkColorSetRGB(0xFE, 0xBC, 0x2E), SkColorSetRGB(0xDE, 0xA1, 0x23),
              SkColorSetRGB(0x99, 0x57, 0x00), IDS_APP_ACCNAME_MINIMIZE};
    case WindowButton::kZoom:
      return {SkColorSetRGB(0x28, 0xC8, 0x40), SkColorSetRGB(0x1A, 0xAB, 0x29),
              SkColorSetRGB(0x00, 0x65, 0x00), IDS_ENTER_FULLSCREEN_MAC};
  }
}

void PaintGlyph(gfx::Canvas* canvas,
                WindowButton button,
                const gfx::PointF& c,
                SkColor color) {
  cc::PaintFlags flags;
  flags.setAntiAlias(true);
  flags.setColor(color);
  switch (button) {
    case WindowButton::kClose:
      flags.setStyle(cc::PaintFlags::kStroke_Style);
      flags.setStrokeWidth(1.3f);
      flags.setStrokeCap(cc::PaintFlags::kRound_Cap);
      canvas->DrawLine(gfx::PointF(c.x() - 2.6f, c.y() - 2.6f),
                       gfx::PointF(c.x() + 2.6f, c.y() + 2.6f), flags);
      canvas->DrawLine(gfx::PointF(c.x() + 2.6f, c.y() - 2.6f),
                       gfx::PointF(c.x() - 2.6f, c.y() + 2.6f), flags);
      break;
    case WindowButton::kMinimize:
      flags.setStyle(cc::PaintFlags::kStroke_Style);
      flags.setStrokeWidth(1.4f);
      flags.setStrokeCap(cc::PaintFlags::kRound_Cap);
      canvas->DrawLine(gfx::PointF(c.x() - 3.5f, c.y()),
                       gfx::PointF(c.x() + 3.5f, c.y()), flags);
      break;
    case WindowButton::kZoom: {
      // Two corners pointing out, as macOS's full-screen glyph.
      SkPath path = SkPathBuilder()
                        .moveTo(c.x() - 3, c.y() - 3)
                        .lineTo(c.x() + 1.2f, c.y() - 3)
                        .lineTo(c.x() - 3, c.y() + 1.2f)
                        .close()
                        .moveTo(c.x() + 3, c.y() + 3)
                        .lineTo(c.x() - 1.2f, c.y() + 3)
                        .lineTo(c.x() + 3, c.y() - 1.2f)
                        .close()
                        .detach();
      canvas->DrawPath(path, flags);
      break;
    }
  }
}

}  // namespace

class FloatingWindowButtons::Light : public views::Button {
  METADATA_HEADER(Light, views::Button)

 public:
  Light(FloatingWindowButtons* owner, WindowButton button)
      : Button(base::BindRepeating(&Light::Press, base::Unretained(this))),
        owner_(owner),
        button_(button) {
    GetViewAccessibility().SetName(
        l10n_util::GetStringUTF16(LookOf(button).name));
    SetFocusBehavior(FocusBehavior::ACCESSIBLE_ONLY);
  }
  Light(const Light&) = delete;
  Light& operator=(const Light&) = delete;
  ~Light() override = default;

  // views::Button:
  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override {
    return gfx::Size(kDiameter, kDiameter);
  }

  void PaintButtonContents(gfx::Canvas* canvas) override {
    const gfx::PointF center = gfx::RectF(GetLocalBounds()).CenterPoint();
    const float radius = kDiameter / 2.0f - 0.5f;
    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    if (!owner_->hovered() && !GetWidget()->ShouldPaintAsActive()) {
      flags.setColor(SkColorSetA(
          GetColorProvider()->GetColor(kColorTabForegroundInactiveFrameActive),
          kFaintAlpha));
      canvas->DrawCircle(center, radius, flags);
      return;
    }
    const Look look = LookOf(button_);
    flags.setColor(look.fill);
    canvas->DrawCircle(center, radius, flags);
    if (GetState() == STATE_PRESSED) {
      flags.setColor(SkColorSetA(SK_ColorBLACK, kPressedAlpha));
      canvas->DrawCircle(center, radius, flags);
    }
    flags.setStyle(cc::PaintFlags::kStroke_Style);
    flags.setStrokeWidth(1.0f);
    flags.setColor(look.rim);
    canvas->DrawCircle(center, radius, flags);
    if (owner_->hovered()) {
      PaintGlyph(canvas, button_, center, look.glyph);
    }
  }

 private:
  void Press(const ui::Event& event) {
    PressWindowButton(GetWidget(), button_, event.IsAltDown());
  }

  const raw_ptr<FloatingWindowButtons> owner_;
  const WindowButton button_;
};

BEGIN_METADATA(FloatingWindowButtons, Light)
END_METADATA

FloatingWindowButtons::FloatingWindowButtons() {
  SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal, gfx::Insets(), kGap));
  for (WindowButton button :
       {WindowButton::kClose, WindowButton::kMinimize, WindowButton::kZoom}) {
    AddChildView(std::make_unique<Light>(this, button));
  }
  SetNotifyEnterExitOnChild(true);
}

FloatingWindowButtons::~FloatingWindowButtons() = default;

void FloatingWindowButtons::OnMouseEntered(const ui::MouseEvent& event) {
  SetHovered(true);
}

void FloatingWindowButtons::OnMouseExited(const ui::MouseEvent& event) {
  SetHovered(false);
}

void FloatingWindowButtons::AddedToWidget() {
  paint_as_active_ =
      GetWidget()->RegisterPaintAsActiveChangedCallback(base::BindRepeating(
          &FloatingWindowButtons::SchedulePaint, base::Unretained(this)));
}

void FloatingWindowButtons::RemovedFromWidget() {
  paint_as_active_ = {};
}

void FloatingWindowButtons::SetHovered(bool hovered) {
  if (hovered_ == hovered) {
    return;
  }
  hovered_ = hovered;
  for (views::View* light : children()) {
    light->SchedulePaint();
  }
}

BEGIN_METADATA(FloatingWindowButtons)
END_METADATA

}  // namespace uw
