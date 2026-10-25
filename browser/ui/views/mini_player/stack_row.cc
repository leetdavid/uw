// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/browser/ui/views/mini_player/stack_row.h"

#include <algorithm>
#include <utility>

#include "base/strings/stringprintf.h"
#include "base/strings/utf_string_conversions.h"
#include "cc/paint/paint_flags.h"
#include "chrome/browser/ui/color/chrome_color_id.h"
#include "chrome/grit/generated_resources.h"
#include "third_party/skia/include/core/SkPath.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/color/color_provider.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/color_utils.h"
#include "ui/gfx/font_list.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/gfx/geometry/skia_conversions.h"
#include "ui/gfx/image/image_skia_operations.h"
#include "ui/gfx/paint_vector_icon.h"
#include "ui/gfx/scoped_canvas.h"
#include "ui/gfx/text_elider.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/controls/button/image_button.h"
#include "ui/views/controls/label.h"
#include "uw/browser/ui/vector_icons/vector_icons.h"

namespace uw {
namespace {

constexpr int kPadding = 4;
constexpr gfx::Size kStill(78, 44);
constexpr int kStillRadius = 7;
constexpr int kRadius = 10;
constexpr int kGap = 9;
constexpr int kDropSize = 21;

gfx::FontList FontOfSize(int size, gfx::Font::Weight weight) {
  const gfx::FontList& base = views::Label::GetDefaultFontList();
  return base.DeriveWithSizeDelta(size - base.GetFontSize())
      .DeriveWithWeight(weight);
}

// "4:12" or "1:02:41".
std::u16string FormatTime(base::TimeDelta time) {
  const int64_t seconds = std::max<int64_t>(0, time.InSeconds());
  const int64_t hours = seconds / 3600;
  return base::UTF8ToUTF16(
      hours > 0 ? base::StringPrintf("%lld:%02lld:%02lld",
                                     static_cast<long long>(hours),
                                     static_cast<long long>(seconds / 60 % 60),
                                     static_cast<long long>(seconds % 60))
                : base::StringPrintf("%lld:%02lld",
                                     static_cast<long long>(seconds / 60),
                                     static_cast<long long>(seconds % 60)));
}

}  // namespace

StackRow::StackRow(const MiniPlayerStack::Entry& entry,
                   PressedCallback play,
                   PressedCallback drop)
    : views::Button(std::move(play)), entry_(entry) {
  GetViewAccessibility().SetName(entry_.title);
  SetTooltipText(entry_.title);
  drop_ = AddChildView(std::make_unique<views::ImageButton>(std::move(drop)));
  const std::u16string close =
      l10n_util::GetStringUTF16(IDS_PICTURE_IN_PICTURE_CLOSE_CONTROL_TEXT);
  drop_->SetTooltipText(close);
  drop_->GetViewAccessibility().SetName(close);
  drop_->SetImageHorizontalAlignment(views::ImageButton::ALIGN_CENTER);
  drop_->SetImageVerticalAlignment(views::ImageButton::ALIGN_MIDDLE);
  drop_->SetVisible(false);
  // The pointer on the row's × is still on the row.
  SetNotifyEnterExitOnChild(true);
}

StackRow::~StackRow() = default;

gfx::Size StackRow::CalculatePreferredSize(
    const views::SizeBounds& available_size) const {
  return gfx::Size(available_size.width().is_bounded()
                       ? available_size.width().value()
                       : width(),
                   kHeight);
}

void StackRow::Layout(PassKey) {
  drop_->SetBounds(width() - kPadding - kDropSize - 4,
                   (height() - kDropSize) / 2, kDropSize, kDropSize);
}

void StackRow::OnThemeChanged() {
  views::Button::OnThemeChanged();
  drop_->SetImageModel(
      views::Button::STATE_NORMAL,
      ui::ImageModel::FromVectorIcon(kMiniPlayerCloseIcon,
                                     SkColorSetA(TextColor(), 0x94), 13));
}

void StackRow::StateChanged(ButtonState old_state) {
  views::Button::StateChanged(old_state);
  drop_->SetVisible(GetState() == STATE_HOVERED ||
                    GetState() == STATE_PRESSED || drop_->IsMouseHovered());
  SchedulePaint();
}

SkColor StackRow::TextColor() const {
  return GetColorProvider() ? GetColorProvider()->GetColor(
                                  kColorTabForegroundInactiveFrameActive)
                            : SK_ColorBLACK;
}

bool StackRow::OnLightSidebar() const {
  return color_utils::IsDark(TextColor());
}

void StackRow::PaintButtonContents(gfx::Canvas* canvas) {
  const bool hovered =
      GetState() == STATE_HOVERED || GetState() == STATE_PRESSED;
  const bool light = OnLightSidebar();
  const gfx::RectF bounds(GetLocalBounds());
  cc::PaintFlags fill;
  fill.setAntiAlias(true);
  fill.setColor(SkColorSetA(
      SK_ColorWHITE, hovered ? (light ? 0xFF : 0x29) : (light ? 0x75 : 0x14)));
  canvas->DrawRoundRect(bounds, kRadius, fill);

  // The still, cropped to fill its box, darkened, with a play mark.
  const gfx::Rect still_box(kPadding, (height() - kStill.height()) / 2,
                            kStill.width(), kStill.height());
  {
    gfx::ScopedCanvas scoped(canvas);
    canvas->ClipPath(
        SkPath::RRect(gfx::RectToSkRect(still_box), kStillRadius, kStillRadius),
        true);
    canvas->FillRect(still_box, SK_ColorBLACK);
    if (!entry_.still.isNull()) {
      const gfx::Size image = entry_.still.size();
      const float scale =
          std::max(static_cast<float>(still_box.width()) / image.width(),
                   static_cast<float>(still_box.height()) / image.height());
      const gfx::Size source(still_box.width() / scale,
                             still_box.height() / scale);
      canvas->DrawImageInt(entry_.still, (image.width() - source.width()) / 2,
                           (image.height() - source.height()) / 2,
                           source.width(), source.height(), still_box.x(),
                           still_box.y(), still_box.width(), still_box.height(),
                           true);
    }
    canvas->FillRect(still_box, SkColorSetA(SK_ColorBLACK, 0x40));
    const gfx::ImageSkia play =
        gfx::CreateVectorIcon(kMiniPlayerPlayIcon, 14, SK_ColorWHITE);
    canvas->DrawImageInt(play, still_box.CenterPoint().x() - 7,
                         still_box.CenterPoint().y() - 7);
  }

  // The title, then the site's icon, a pause mark, and where it stopped.
  const SkColor text = TextColor();
  const int left = still_box.right() + kGap;
  const int right =
      width() - kPadding - (drop_->GetVisible() ? kDropSize + 4 : 4);
  canvas->DrawStringRectWithFlags(
      entry_.title, FontOfSize(13, gfx::Font::Weight::MEDIUM), text,
      gfx::Rect(left, 9, right - left, 17), gfx::Canvas::NO_SUBPIXEL_RENDERING);
  int x = left;
  const int line = 31;
  const SkColor faint = SkColorSetA(text, 0x94);
  if (!entry_.favicon.isNull()) {
    canvas->DrawImageInt(gfx::ImageSkiaOperations::CreateResizedImage(
                             entry_.favicon, skia::ImageOperations::RESIZE_BEST,
                             gfx::Size(12, 12)),
                         x, line);
    x += 12 + 5;
  }
  // A site's own window has no place to stop at: it reads "Paused".
  if (entry_.play_site_window) {
    canvas->DrawStringRectWithFlags(
        l10n_util::GetStringUTF16(IDS_UW_PLAYBACK_CONTROLS_PAUSED),
        FontOfSize(11, gfx::Font::Weight::NORMAL), faint,
        gfx::Rect(x, line - 1, right - x, 14),
        gfx::Canvas::NO_SUBPIXEL_RENDERING);
    return;
  }
  canvas->DrawImageInt(gfx::CreateVectorIcon(kMiniPlayerPauseIcon, 10, faint),
                       x, line + 1);
  x += 10 + 3;
  canvas->DrawStringRectWithFlags(FormatTime(entry_.paused_at),
                                  FontOfSize(11, gfx::Font::Weight::NORMAL),
                                  faint, gfx::Rect(x, line - 1, right - x, 14),
                                  gfx::Canvas::NO_SUBPIXEL_RENDERING);
}

BEGIN_METADATA(StackRow)
END_METADATA

}  // namespace uw
