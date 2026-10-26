// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/browser/ui/views/mini_player/mini_player_dock.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include "base/functional/bind.h"
#include "cc/paint/paint_flags.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/picture_in_picture/scoped_disallow_picture_in_picture.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/color/chrome_color_id.h"
#include "third_party/skia/include/core/SkPath.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/color/color_provider.h"
#include "ui/compositor/layer.h"
#include "ui/gfx/animation/tween.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/color_utils.h"
#include "ui/gfx/geometry/skia_conversions.h"
#include "ui/gfx/scoped_canvas.h"
#include "ui/views/view_class_properties.h"
#include "uw/browser/ui/views/mini_player/auto_mini_player.h"
#include "uw/browser/ui/views/mini_player/mini_player_prefs.h"
#include "uw/browser/ui/views/mini_player/mini_player_stack.h"
#include "uw/browser/ui/views/mini_player/mini_player_view.h"
#include "uw/browser/ui/views/mini_player/site_window.h"
#include "uw/browser/ui/views/mini_player/stack_row.h"

namespace uw {
namespace {

// The gap to the activity card above and the playback controls or the foot
// below, and between a video and a site's page.
constexpr int kGap = 6;

// A video before its size is known.
constexpr gfx::Size kDefaultShape(16, 9);

// Each paused video peeks this much above the one in front of it, this much
// narrower on each side.
constexpr int kPeek = 5;
constexpr int kPeekInset = 6;
constexpr int kRowGap = 6;
// The rows slide up quickly and settle; they drop back a little faster.
constexpr base::TimeDelta kFanIn = base::Milliseconds(220);
constexpr base::TimeDelta kFanOut = base::Milliseconds(160);
constexpr int kCornerRadius = 10;

// `shape` in `width`: as wide as the dock, and no taller than it is wide. A
// tall one is as tall as the dock is wide, and narrower.
gfx::Size FitShape(gfx::Size shape, int width) {
  int height =
      std::round(static_cast<double>(width) * shape.height() / shape.width());
  if (height <= width) {
    return gfx::Size(width, height);
  }
  height = width;
  return gfx::Size(
      std::round(static_cast<double>(height) * shape.width() / shape.height()),
      height);
}

}  // namespace

DEFINE_USER_DATA(MiniPlayerDock);

MiniPlayerDock::MiniPlayerDock(BrowserWindowInterface* window)
    : window_(window),
      auto_mini_player_(std::make_unique<AutoMiniPlayer>(window)),
      user_data_(window->GetUnownedUserDataHost(), *this) {
  SetVisible(false);
  SetNotifyEnterExitOnChild(true);
  SetProperty(views::kMarginsKey, gfx::Insets::VH(kGap, 0));
  stack_subscription_ = MiniPlayerStack::Get().Observe(base::BindRepeating(
      &MiniPlayerDock::OnStackChanged, base::Unretained(this)));
  if (PrefService* local_state = g_browser_process->local_state()) {
    prefs_.Init(local_state);
    prefs_.Add(kMiniPlayerModePref,
               base::BindRepeating(&MiniPlayerDock::UpdateOff,
                                   base::Unretained(this)));
  }
  UpdateOff();
  SiteWindow::Watch();
}

MiniPlayerDock::~MiniPlayerDock() {
  if (player_ && on_destroying_) {
    std::move(on_destroying_).Run();
  }
  if (site_window_ && on_site_window_destroying_) {
    std::move(on_site_window_destroying_).Run();
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
  SetVisible(site_window_ != nullptr);
  OnStackChanged();
  PreferredSizeChanged();
  return RemoveChildViewT(player);
}

views::View* MiniPlayerDock::TakeSiteWindow(std::unique_ptr<views::View> page,
                                            gfx::Size shape) {
  site_window_ = AddChildView(std::move(page));
  site_window_shape_ = shape;
  SetVisible(true);
  PreferredSizeChanged();
  return site_window_;
}

std::unique_ptr<views::View> MiniPlayerDock::ReleaseSiteWindow() {
  views::View* page = site_window_;
  site_window_ = nullptr;
  on_site_window_destroying_.Reset();
  SetVisible(player_ != nullptr);
  PreferredSizeChanged();
  return page ? RemoveChildViewT(page) : nullptr;
}

void MiniPlayerDock::SetSiteWindowShape(gfx::Size shape) {
  site_window_shape_ = shape;
  PreferredSizeChanged();
}

void MiniPlayerDock::UpdateShape() {
  PreferredSizeChanged();
}

gfx::Size MiniPlayerDock::CalculatePreferredSize(
    const views::SizeBounds& available_size) const {
  const int available = available_size.width().is_bounded()
                            ? available_size.width().value()
                            : width();
  const int site_window = SiteWindowSize(available).height();
  return gfx::Size(available, StackHeight() + PlayerSize(available).height() +
                                  (player_ && site_window ? kGap : 0) +
                                  site_window);
}

void MiniPlayerDock::Layout(PassKey) {
  // A site's page sits at the bottom, under any video.
  const gfx::Size site_window = SiteWindowSize(width());
  if (site_window_) {
    gfx::Rect bounds(0, height() - site_window.height(), width(),
                     site_window.height());
    bounds.ClampToCenteredSize(site_window);
    site_window_->SetBoundsRect(bounds);
  }
  if (!player_) {
    return;
  }
  const int top = StackHeight();
  const int player_height = PlayerSize(width()).height();
  gfx::Rect bounds(0, top, width(), player_height);
  bounds.ClampToCenteredSize(PlayerSize(width()));
  player_->SetBoundsRect(bounds);
  // Each paused video rises from where it peeks out behind the playing one to
  // its row, the newest nearest, widening as it goes.
  const double fan = fan_.GetCurrentValue();
  for (size_t i = 0; i < rows_.size(); ++i) {
    const int step = static_cast<int>(i) + 1;
    const int y = gfx::Tween::IntValueBetween(
        fan, top - kPeek * step, top - (StackRow::kHeight + kRowGap) * step);
    const int inset = gfx::Tween::IntValueBetween(fan, kPeekInset * step, 0);
    rows_[i]->SetBounds(inset, y, width() - 2 * inset, StackRow::kHeight);
  }
}

void MiniPlayerDock::OnPaint(gfx::Canvas* canvas) {
  views::View::OnPaint(canvas);
  const double fan = fan_.GetCurrentValue();
  if (!player_ || fan >= 1) {
    return;
  }
  // The paused videos peek out above the playing one, the oldest farthest.
  const std::vector<MiniPlayerStack::Entry>& paused =
      MiniPlayerStack::Get().paused();
  const gfx::Rect front = player_->bounds();
  // The peeks fade as the rows rise out of them.
  gfx::ScopedCanvas faded(canvas);
  canvas->SaveLayerAlpha(static_cast<uint8_t>(255 * (1 - fan)));
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

void MiniPlayerDock::AnimationProgressed(const gfx::Animation* animation) {
  UpdateFan();
}

void MiniPlayerDock::AnimationEnded(const gfx::Animation* animation) {
  UpdateFan();
}

gfx::Size MiniPlayerDock::PlayerSize(int width) const {
  if (!player_) {
    return gfx::Size();
  }
  return FitShape(player_->natural_size().IsEmpty() ? kDefaultShape
                                                    : player_->natural_size(),
                  width);
}

gfx::Size MiniPlayerDock::SiteWindowSize(int width) const {
  if (!site_window_ || site_window_shape_.IsEmpty()) {
    return gfx::Size();
  }
  return FitShape(site_window_shape_, width);
}

int MiniPlayerDock::StackHeight() const {
  if (!player_) {
    return 0;
  }
  const int paused = static_cast<int>(rows_.size());
  return gfx::Tween::IntValueBetween(fan_.GetCurrentValue(), paused * kPeek,
                                     paused * (StackRow::kHeight + kRowGap));
}

void MiniPlayerDock::UpdateFan() {
  const double fan = fan_.GetCurrentValue();
  for (raw_ptr<StackRow>& row : rows_) {
    row->SetVisible(fan > 0);
    row->layer()->SetOpacity(fan);
  }
  PreferredSizeChanged();
  SchedulePaint();
}

void MiniPlayerDock::UpdateOff() {
  if (MiniPlayerModeOf(g_browser_process->local_state()) !=
      MiniPlayerMode::kOff) {
    off_.reset();
  } else if (!off_) {
    off_ = std::make_unique<ScopedDisallowPictureInPicture>();
  }
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
      // Behind the playing video, so the rows rise out from under it.
      auto* row = AddChildViewAt(
          std::make_unique<StackRow>(
              paused[i],
              base::BindRepeating(&MiniPlayerStack::Play,
                                  base::Unretained(&MiniPlayerStack::Get()), i),
              base::BindRepeating(&MiniPlayerStack::Drop,
                                  base::Unretained(&MiniPlayerStack::Get()),
                                  i)),
          0);
      row->SetPaintToLayer();
      row->layer()->SetFillsBoundsOpaquely(false);
      rows_.push_back(row);
    }
  }
  if (rows_.empty()) {
    fanned_ = false;
    fan_.Reset(0);
  }
  UpdateFan();
}

void MiniPlayerDock::SetFanned(bool fanned) {
  fanned = fanned && !rows_.empty();
  if (fanned_ == fanned) {
    return;
  }
  fanned_ = fanned;
  if (gfx::Animation::PrefersReducedMotion()) {
    fan_.Reset(fanned_ ? 1 : 0);
    UpdateFan();
    return;
  }
  fan_.SetSlideDuration(fanned_ ? kFanIn : kFanOut);
  fan_.SetTweenType(fanned_ ? gfx::Tween::EASE_OUT : gfx::Tween::EASE_IN);
  if (fanned_) {
    fan_.Show();
  } else {
    fan_.Hide();
  }
}

BEGIN_METADATA(MiniPlayerDock)
END_METADATA

}  // namespace uw
