// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/browser/ui/views/mini_player/mini_player_dock.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/color/chrome_color_id.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/color/color_provider.h"
#include "ui/gfx/color_utils.h"
#include "ui/views/view_class_properties.h"
#include "uw/browser/ui/views/mini_player/auto_mini_player.h"
#include "uw/browser/ui/views/mini_player/mini_player_view.h"

namespace uw {
namespace {

// The gap to the activity card above.
constexpr int kTopGap = 6;

// A video before its size is known.
constexpr gfx::Size kDefaultShape(16, 9);

}  // namespace

DEFINE_USER_DATA(MiniPlayerDock);

MiniPlayerDock::MiniPlayerDock(BrowserWindowInterface* window)
    : window_(window),
      auto_mini_player_(std::make_unique<AutoMiniPlayer>(window)),
      user_data_(window->GetUnownedUserDataHost(), *this) {
  SetVisible(false);
  SetProperty(views::kMarginsKey, gfx::Insets::TLBR(kTopGap, 0, 0, 0));
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
  PreferredSizeChanged();
  return player_;
}

std::unique_ptr<MiniPlayerView> MiniPlayerDock::Release() {
  MiniPlayerView* player = player_;
  player_ = nullptr;
  on_destroying_.Reset();
  SetVisible(false);
  PreferredSizeChanged();
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
  return gfx::Size(available, PlayerSize(available).height());
}

void MiniPlayerDock::Layout(PassKey) {
  if (!player_) {
    return;
  }
  gfx::Rect bounds = GetLocalBounds();
  bounds.ClampToCenteredSize(PlayerSize(width()));
  player_->SetBoundsRect(bounds);
}

void MiniPlayerDock::OnThemeChanged() {
  views::View::OnThemeChanged();
  UpdateLook();
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

BEGIN_METADATA(MiniPlayerDock)
END_METADATA

}  // namespace uw
