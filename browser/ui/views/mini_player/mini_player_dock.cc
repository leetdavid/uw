// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/browser/ui/views/mini_player/mini_player_dock.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include "base/functional/bind.h"
#include "cc/paint/paint_flags.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/color/chrome_color_id.h"
#include "third_party/skia/include/core/SkPath.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/color/color_provider.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/color_utils.h"
#include "ui/gfx/geometry/skia_conversions.h"
#include "ui/gfx/scoped_canvas.h"
#include "ui/views/view_class_properties.h"
#include "uw/browser/ui/views/mini_player/auto_mini_player.h"
#include "uw/browser/ui/views/mini_player/mini_player_stack.h"
#include "uw/browser/ui/views/mini_player/mini_player_view.h"
#include "uw/browser/ui/views/mini_player/stack_row.h"

namespace uw {
namespace {

// The gap to the activity card above.
constexpr int kTopGap = 6;

// A video before its size is known.
constexpr gfx::Size kDefaultShape(16, 9);

// Each paused video peeks this much above the one in front of it, this much
// narrower on each side.
constexpr int kPeek = 5;
constexpr int kPeekInset = 6;
constexpr int kRowGap = 6;
constexpr int kCornerRadius = 10;

}  // namespace

DEFINE_USER_DATA(MiniPlayerDock);

MiniPlayerDock::MiniPlayerDock(BrowserWindowInterface* window)
    : window_(window),
      auto_mini_player_(std::make_unique<AutoMiniPlayer>(window)),
      user_data_(window->GetUnownedUserDataHost(), *this) {
  SetVisible(false);
  SetNotifyEnterExitOnChild(true);
  SetProperty(views::kMarginsKey, gfx::Insets::TLBR(kTopGap, 0, 0, 0));
  stack_subscription_ = MiniPlayerStack::Get().Observe(base::BindRepeating(
      &MiniPlayerDock::OnStackChanged, base::Unretained(this)));
}

MiniPlayerDock::~MiniPlayerDock() {
  if (player_ && on_destroying_) {
    std::move(on_destroying_).Run();
  }
}

// static
MiniPlayerDock* MiniPlayerDock::From(BrowserWindowInterface* window) {
  return window ? Get(window->GetUnownedUserDataHost()) : nullptr;
}

MiniPlayerView* MiniPlayerDock::Take(std::unique_ptr<MiniPlayerView> player) {
  player_ = AddChildView(std::move(player));
  UpdateLook();
  SetVisible(true);
  OnStackChanged();
  return player_;
}

std::unique_ptr<MiniPlayerView> MiniPlayerDock::Release() {
  MiniPlayerView* player = player_;
  player_ = nullptr;
  on_destroying_.Reset();
  SetVisible(false);
  OnStackChanged();
  return RemoveChildViewT(player);
}

void MiniPlayerDock::UpdateShape() {
  PreferredSizeChanged();
}

gfx::Size MiniPlayerDock::CalculatePreferredSize(
    const views::SizeBounds& available_size) const {
  const int available = available_size.width().is_bounded()
                            ? available_size.width().value()
                            : width();
  return gfx::Size(available, StackHeight() + PlayerSize(available).height());
}

void MiniPlayerDock::Layout(PassKey) {
  if (!player_) {
    return;
  }
  // Fanned out, the newest paused video sits just above the playing one.
  int y = 0;
  for (auto it = rows_.rbegin(); it != rows_.rend(); ++it) {
    (*it)->SetBounds(0, y, width(), StackRow::kHeight);
    y += StackRow::kHeight + kRowGap;
  }
  gfx::Rect bounds(0, StackHeight(), width(), height() - StackHeight());
  bounds.ClampToCenteredSize(PlayerSize(width()));
  player_->SetBoundsRect(bounds);
}

void MiniPlayerDock::OnPaint(gfx::Canvas* canvas) {
  views::View::OnPaint(canvas);
  if (!player_ || fanned_) {
    return;
  }
  // The paused videos peek out above the playing one, the oldest farthest.
  const std::vector<MiniPlayerStack::Entry>& paused =
      MiniPlayerStack::Get().paused();
  const gfx::Rect front = player_->bounds();
  for (size_t i = paused.size(); i-- > 0;) {
    const int step = static_cast<int>(i) + 1;
    gfx::Rect peek = front;
    peek.Inset(gfx::Insets::VH(0, kPeekInset * step));
    peek.Offset(0, -kPeek * step);
    gfx::ScopedCanvas scoped(canvas);
    canvas->ClipPath(
        SkPath::RRect(gfx::RectToSkRect(peek), kCornerRadius, kCornerRadius),
        true);
    canvas->FillRect(peek, SK_ColorBLACK);
    const gfx::ImageSkia& still = paused[i].still;
    if (!still.isNull()) {
      canvas->DrawImageInt(still, 0, 0, still.width(), still.height(), peek.x(),
                           peek.y(), peek.width(), peek.height(), true);
    }
    canvas->FillRect(peek, SkColorSetA(SK_ColorBLACK, 0x52));
  }
}

void MiniPlayerDock::OnThemeChanged() {
  views::View::OnThemeChanged();
  UpdateLook();
}

void MiniPlayerDock::OnMouseEntered(const ui::MouseEvent& event) {
  SetFanned(true);
}

void MiniPlayerDock::OnMouseExited(const ui::MouseEvent& event) {
  SetFanned(false);
}

gfx::Size MiniPlayerDock::PlayerSize(int width) const {
  const gfx::Size shape = player_ && !player_->natural_size().IsEmpty()
                              ? player_->natural_size()
                              : kDefaultShape;
  int height =
      std::round(static_cast<double>(width) * shape.height() / shape.width());
  if (height <= width) {
    return gfx::Size(width, height);
  }
  // A tall video is as tall as the dock is wide, and narrower.
  height = width;
  return gfx::Size(
      std::round(static_cast<double>(height) * shape.width() / shape.height()),
      height);
}

int MiniPlayerDock::StackHeight() const {
  if (!player_) {
    return 0;
  }
  const int paused = static_cast<int>(rows_.size());
  return fanned_ ? paused * (StackRow::kHeight + kRowGap) : paused * kPeek;
}

void MiniPlayerDock::UpdateLook() {
  if (!player_ || !GetColorProvider()) {
    return;
  }
  // The ring is the sidebar's text color, faint; a light sidebar adds a soft
  // shadow under the video.
  const SkColor text =
      GetColorProvider()->GetColor(kColorTabForegroundInactiveFrameActive);
  player_->SetDockedLook(SkColorSetA(text, 0x1F),
                         /*shadow=*/color_utils::IsDark(text));
  InvalidateLayout();
}

void MiniPlayerDock::OnStackChanged() {
  for (raw_ptr<StackRow>& row : rows_) {
    RemoveChildViewT(row.ExtractAsDangling().get());
  }
  rows_.clear();
  if (player_) {
    const std::vector<MiniPlayerStack::Entry>& paused =
        MiniPlayerStack::Get().paused();
    for (size_t i = 0; i < paused.size(); ++i) {
      auto* row = AddChildView(std::make_unique<StackRow>(
          paused[i],
          base::BindRepeating(&MiniPlayerStack::Play,
                              base::Unretained(&MiniPlayerStack::Get()), i),
          base::BindRepeating(&MiniPlayerStack::Drop,
                              base::Unretained(&MiniPlayerStack::Get()), i)));
      row->SetVisible(fanned_);
      rows_.push_back(row);
    }
  }
  if (rows_.empty()) {
    fanned_ = false;
  }
  PreferredSizeChanged();
  SchedulePaint();
}

void MiniPlayerDock::SetFanned(bool fanned) {
  fanned = fanned && !rows_.empty();
  if (fanned_ == fanned) {
    return;
  }
  fanned_ = fanned;
  for (raw_ptr<StackRow>& row : rows_) {
    row->SetVisible(fanned_);
  }
  PreferredSizeChanged();
  SchedulePaint();
}

BEGIN_METADATA(MiniPlayerDock)
END_METADATA

}  // namespace uw
