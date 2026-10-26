// Copyright 2026 The uw Authors. All rights reserved.

#ifndef UW_BROWSER_UI_VIEWS_MINI_PLAYER_MINI_PLAYER_H_
#define UW_BROWSER_UI_VIEWS_MINI_PLAYER_MINI_PLAYER_H_

#include <memory>
#include <string>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "build/build_config.h"
#include "chrome/browser/picture_in_picture/picture_in_picture_window.h"
#include "content/public/browser/overlay_window.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/size.h"
#include "ui/views/widget/widget.h"
#include "ui/views/widget/widget_delegate.h"

class PictureInPictureWidgetFadeAnimator;

namespace content {
class VideoPictureInPictureWindowController;
}

namespace uw {

class MiniPlayerDock;
class MiniPlayerPlacement;
class MiniPlayerView;

namespace internal {

// Holds the floating window's delegate. As a base class declared before
// views::Widget, it outlives the window, which uses the delegate until the end.
struct MiniPlayerDelegateHolder {
  MiniPlayerDelegateHolder();
  ~MiniPlayerDelegateHolder();
  std::unique_ptr<views::WidgetDelegate> delegate;
};

}  // namespace internal

// uw's picture in picture for a video, in place of Chromium's window, per
// docs/plans/mini-player.md. It floats over every other window with rounded
// corners and Arc's controls, and keeps the video's shape. Chromium's
// controller owns it and tells it what the video does.
class MiniPlayer : private internal::MiniPlayerDelegateHolder,
                   public content::VideoOverlayWindow,
                   public views::Widget,
                   public PictureInPictureWindow {
 public:
  static std::unique_ptr<content::VideoOverlayWindow> Create(
      content::VideoPictureInPictureWindowController* controller);

  MiniPlayer(const MiniPlayer&) = delete;
  MiniPlayer& operator=(const MiniPlayer&) = delete;
  ~MiniPlayer() override;

  MiniPlayerView* view() { return view_; }
  // The dock the video is in, or nullptr while it floats or is hidden.
  MiniPlayerDock* dock() { return dock_; }
  MiniPlayerPlacement* placement() { return placement_.get(); }

  // content::VideoOverlayWindow:
  bool IsActive() const override;
  void Close() override;
  void ShowInactive() override;
  void Hide() override;
  bool IsVisible() const override;
  gfx::Rect GetBounds() override;
  void UpdateNaturalSize(const gfx::Size& natural_size) override;
  void SetPlaybackState(PlaybackState playback_state) override;
  void SetPlayPauseButtonVisibility(bool is_visible) override;
  void SetSkipAdButtonVisibility(bool is_visible) override;
  void SetNextTrackButtonVisibility(bool is_visible) override;
  void SetPreviousTrackButtonVisibility(bool is_visible) override;
  void SetHidePictureInPictureButtonVisibility(bool is_visible) override {}
  void SetMicrophoneMuted(bool muted) override;
  void SetCameraState(bool turned_on) override;
  void SetMediaMuted(bool muted) override;
  void SetToggleMicrophoneButtonVisibility(bool is_visible) override;
  void SetToggleCameraButtonVisibility(bool is_visible) override;
  void SetHangUpButtonVisibility(bool is_visible) override;
  void SetNextSlideButtonVisibility(bool is_visible) override;
  void SetPreviousSlideButtonVisibility(bool is_visible) override;
  void SetMediaPosition(const media_session::MediaPosition& position) override;
  void SetSourceTitle(const std::u16string& source_title) override {}
  void SetFaviconImages(
      const std::vector<media_session::MediaImage>& images) override {}
  void SetSurfaceId(const viz::SurfaceId& surface_id) override;
  void SetPlaybackControlsVisibility(bool is_visible) override;
  void SetImmersiveVideoOptions(
      const content::ImmersiveOptions& options) override {}

  // views::Widget:
  gfx::Size GetMinimumSize() const override;
  gfx::Size GetMaximumSize() const override;
  void OnNativeWidgetMove() override;
  void OnNativeWidgetSizeChanged(const gfx::Size& new_size) override;
  void OnNativeWidgetDestroying() override;
  void OnNativeWidgetDestroyed() override;

  // PictureInPictureWindow:
  void SetForcedTucking(bool tuck) override;
#if BUILDFLAG(IS_MAC)
  void OnAnyBrowserEnteredFullscreen() override;
#endif

 private:
  explicit MiniPlayer(
      content::VideoPictureInPictureWindowController* controller);

  // Where the window goes: the size and corner it was last left at, keeping
  // the video's shape and inside the screen.
  gfx::Rect CalculateBounds();
  gfx::Rect GetWorkArea() const;

  // Docks the video in `dock`, or floats it when nullptr, while it shows.
  void Place(MiniPlayerDock* dock);
  // Pops a docked video out, or docks everything floating, for the button
  // just left of back to tab.
  void MoveByHand();
  // Brings the video back from its dock into the floating window, hidden.
  void Undock();
  void ShowFloating();

  const raw_ptr<content::VideoPictureInPictureWindowController> controller_;
  raw_ptr<MiniPlayerView> view_ = nullptr;
  // The floating window's client area, where the video floats.
  raw_ptr<views::View> client_ = nullptr;
  raw_ptr<MiniPlayerDock> dock_ = nullptr;
  std::unique_ptr<MiniPlayerPlacement> placement_;
  // True from ShowInactive() to Hide(): docked or floating.
  bool shown_ = false;
  std::unique_ptr<PictureInPictureWidgetFadeAnimator> fade_animator_;
  gfx::Size max_size_;
  bool has_been_shown_ = false;
  // Closing a video that can be paused pauses it.
  bool play_pause_shown_ = false;
};

}  // namespace uw

#endif  // UW_BROWSER_UI_VIEWS_MINI_PLAYER_MINI_PLAYER_H_
