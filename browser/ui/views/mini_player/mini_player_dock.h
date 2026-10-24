// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#ifndef UW_BROWSER_UI_VIEWS_MINI_PLAYER_MINI_PLAYER_DOCK_H_
#define UW_BROWSER_UI_VIEWS_MINI_PLAYER_MINI_PLAYER_DOCK_H_

#include <memory>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/base/unowned_user_data/scoped_unowned_user_data.h"
#include "ui/views/view.h"

class BrowserWindowInterface;

namespace uw {

class MiniPlayerView;

// The place at the bottom of a window's sidebar, above the foot, where the
// mini player docks, per docs/plans/mini-player.md. It spans the sidebar and
// takes the video's shape, never taller than it is wide. Hidden while empty.
class MiniPlayerDock : public views::View {
  METADATA_HEADER(MiniPlayerDock, views::View)

 public:
  DECLARE_USER_DATA(MiniPlayerDock);

  explicit MiniPlayerDock(BrowserWindowInterface* window);
  MiniPlayerDock(const MiniPlayerDock&) = delete;
  MiniPlayerDock& operator=(const MiniPlayerDock&) = delete;
  ~MiniPlayerDock() override;

  static MiniPlayerDock* From(BrowserWindowInterface* window);

  BrowserWindowInterface* window() { return window_; }

  // Docks `player` here, in the docked look.
  MiniPlayerView* Take(std::unique_ptr<MiniPlayerView> player);
  // Gives back the mini player docked here.
  std::unique_ptr<MiniPlayerView> Release();
  MiniPlayerView* player() { return player_; }
  // Fits the dock to the video's shape again.
  void UpdateShape();
  // Called when the dock goes, with its window, while a player is docked. The
  // player's owner releases it then.
  void set_on_destroying(base::OnceClosure on_destroying) {
    on_destroying_ = std::move(on_destroying);
  }

  // views::View:
  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override;
  void Layout(PassKey) override;
  void OnThemeChanged() override;

 private:
  // Where the video goes in `width`: the dock's width, the video's shape, and
  // no taller than it is wide.
  gfx::Size PlayerSize(int width) const;
  void UpdateLook();

  const raw_ptr<BrowserWindowInterface> window_;
  raw_ptr<MiniPlayerView> player_ = nullptr;
  base::OnceClosure on_destroying_;
  ui::ScopedUnownedUserData<MiniPlayerDock> user_data_;
};

}  // namespace uw

#endif  // UW_BROWSER_UI_VIEWS_MINI_PLAYER_MINI_PLAYER_DOCK_H_
