// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#ifndef UW_BROWSER_UI_VIEWS_MINI_PLAYER_MINI_PLAYER_DOCK_H_
#define UW_BROWSER_UI_VIEWS_MINI_PLAYER_MINI_PLAYER_DOCK_H_

#include <memory>
#include <vector>

#include "base/callback_list.h"
#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/base/unowned_user_data/scoped_unowned_user_data.h"
#include "ui/views/view.h"

class BrowserWindowInterface;

namespace uw {

class AutoMiniPlayer;
class MiniPlayerView;
class StackRow;

// The place at the bottom of a window's sidebar, above the foot, where the
// mini player docks, per docs/plans/mini-player.md. It spans the sidebar and
// takes the video's shape, never taller than it is wide. Paused videos of the
// stack peek out above it, and fan out as rows under the pointer. Hidden while
// empty. It also opens a mini player when the user leaves a tab whose video
// plays.
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

  // True while the paused videos show as rows.
  bool fanned() const { return fanned_; }
  const std::vector<raw_ptr<StackRow>>& rows() const { return rows_; }

  // views::View:
  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override;
  void Layout(PassKey) override;
  void OnPaint(gfx::Canvas* canvas) override;
  void OnThemeChanged() override;
  void OnMouseEntered(const ui::MouseEvent& event) override;
  void OnMouseExited(const ui::MouseEvent& event) override;

 private:
  // Where the video goes in `width`: the dock's width, the video's shape, and
  // no taller than it is wide.
  gfx::Size PlayerSize(int width) const;
  // The space above the video: peeks at rest, rows fanned out.
  int StackHeight() const;
  void UpdateLook();
  void OnStackChanged();
  void SetFanned(bool fanned);

  const raw_ptr<BrowserWindowInterface> window_;
  raw_ptr<MiniPlayerView> player_ = nullptr;
  std::vector<raw_ptr<StackRow>> rows_;
  bool fanned_ = false;
  base::OnceClosure on_destroying_;
  std::unique_ptr<AutoMiniPlayer> auto_mini_player_;
  base::CallbackListSubscription stack_subscription_;
  ui::ScopedUnownedUserData<MiniPlayerDock> user_data_;
};

}  // namespace uw

#endif  // UW_BROWSER_UI_VIEWS_MINI_PLAYER_MINI_PLAYER_DOCK_H_
