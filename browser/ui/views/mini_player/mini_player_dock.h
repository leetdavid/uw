// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#ifndef UW_BROWSER_UI_VIEWS_MINI_PLAYER_MINI_PLAYER_DOCK_H_
#define UW_BROWSER_UI_VIEWS_MINI_PLAYER_MINI_PLAYER_DOCK_H_

#include <memory>
#include <vector>

#include "base/callback_list.h"
#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "components/prefs/pref_change_registrar.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/base/unowned_user_data/scoped_unowned_user_data.h"
#include "ui/gfx/animation/animation_delegate.h"
#include "ui/gfx/animation/slide_animation.h"
#include "ui/views/view.h"

class BrowserWindowInterface;
class ScopedDisallowPictureInPicture;

namespace uw {

class AutoMiniPlayer;
class MiniPlayerView;
class StackRow;

// The place at the bottom of a window's sidebar, above the foot, where the
// mini player docks, per docs/plans/mini-player.md. It spans the sidebar and
// takes the video's shape, never taller than it is wide. A site's own window,
// a live window, docks under the video in the same way. Paused videos of the
// stack peek out above it, and slide up into rows under the pointer. Hidden
// while empty. It also opens a mini player when the user leaves a tab whose
// video plays, and keeps picture in picture from opening while the setting is
// Off. With another app active, it watches whether that app's windows cover
// what is docked, which then floats.
class MiniPlayerDock : public views::View, public gfx::AnimationDelegate {
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

  // Docks a site's own page here, under any video, in `shape`, the size the
  // site asked for.
  views::View* TakeSiteWindow(std::unique_ptr<views::View> page,
                              gfx::Size shape);
  // Gives back the site's page docked here.
  std::unique_ptr<views::View> ReleaseSiteWindow();
  views::View* site_window() { return site_window_; }
  void SetSiteWindowShape(gfx::Size shape);
  // Called when the dock goes, with its window, while a site's page is docked.
  void set_on_site_window_destroying(base::OnceClosure on_destroying) {
    on_site_window_destroying_ = std::move(on_destroying);
  }
  // Called when the dock goes, with its window, while a player is docked. The
  // player's owner releases it then.
  void set_on_destroying(base::OnceClosure on_destroying) {
    on_destroying_ = std::move(on_destroying);
  }

  // True once, with another app active, other apps' windows have covered half
  // or more of what is docked here for about a second, until uw comes to the
  // front again.
  bool covered() const { return covered_; }
  // Runs `callback` each time covered() changes.
  base::CallbackListSubscription ObserveCover(base::RepeatingClosure callback);
  // Where the mini player docks, in screen coordinates, even while nothing is
  // docked: then the bottom of the sidebar, as tall as it is wide.
  gfx::Rect PlaceInScreen() const;

  // True while the paused videos show as rows, or slide up to them.
  bool fanned() const { return fanned_; }
  bool IsFanAnimating() const { return fan_.is_animating(); }
  const std::vector<raw_ptr<StackRow>>& rows() const { return rows_; }

  // views::View:
  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override;
  void Layout(PassKey) override;
  void OnPaint(gfx::Canvas* canvas) override;
  void OnThemeChanged() override;
  void OnMouseEntered(const ui::MouseEvent& event) override;
  void OnMouseExited(const ui::MouseEvent& event) override;

  // gfx::AnimationDelegate:
  void AnimationProgressed(const gfx::Animation* animation) override;
  void AnimationEnded(const gfx::Animation* animation) override;

 private:
  // Where the video goes in `width`: the dock's width, the video's shape, and
  // no taller than it is wide.
  gfx::Size PlayerSize(int width) const;
  gfx::Size SiteWindowSize(int width) const;
  // The space above the video: peeks at rest, rows fanned out, and between
  // the two while they slide.
  int StackHeight() const;
  // Shows the rows as far along as the slide is.
  void UpdateFan();
  void UpdateLook();
  void OnStackChanged();
  void SetFanned(bool fanned);
  void UpdateOff();
  // Checks twice a second whether what is docked is covered, while another
  // app is active, something is docked, and floating when covered is on.
  void UpdateCoverCheck();
  void CheckCover();
  void SetCovered(bool covered);

  const raw_ptr<BrowserWindowInterface> window_;
  raw_ptr<MiniPlayerView> player_ = nullptr;
  std::vector<raw_ptr<StackRow>> rows_;
  bool fanned_ = false;
  // From 0, the paused videos peeking, to 1, the rows fanned out.
  gfx::SlideAnimation fan_{this};
  base::OnceClosure on_destroying_;
  raw_ptr<views::View> site_window_ = nullptr;
  gfx::Size site_window_shape_;
  base::OnceClosure on_site_window_destroying_;
  std::unique_ptr<AutoMiniPlayer> auto_mini_player_;
  PrefChangeRegistrar prefs_;
  // Chromium's own block on picture in picture, held while it is Off.
  std::unique_ptr<ScopedDisallowPictureInPicture> off_;
  base::CallbackListSubscription stack_subscription_;
  bool covered_ = false;
  // When the latest check found it covered, while it has stayed so.
  base::TimeTicks covered_since_;
  base::RepeatingTimer cover_check_;
  base::RepeatingClosureList cover_callbacks_;
  base::CallbackListSubscription app_activity_;
  ui::ScopedUnownedUserData<MiniPlayerDock> user_data_;
};

}  // namespace uw

#endif  // UW_BROWSER_UI_VIEWS_MINI_PLAYER_MINI_PLAYER_DOCK_H_
