// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#ifndef UW_BROWSER_UI_VIEWS_MINI_PLAYER_MINI_PLAYER_VIEW_H_
#define UW_BROWSER_UI_VIEWS_MINI_PLAYER_MINI_PLAYER_VIEW_H_

#include <optional>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/timer/timer.h"
#include "components/viz/common/surfaces/surface_id.h"
#include "content/public/browser/overlay_window.h"
#include "services/media_session/public/cpp/media_position.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/gfx/geometry/point.h"
#include "ui/gfx/geometry/size.h"
#include "ui/views/view.h"

namespace content {
class VideoPictureInPictureWindowController;
}

namespace ui {
class Compositor;
}

namespace views {
class Label;
}

namespace uw {

class FastLabel;
class MiniPlayerButton;
class MiniPlayerProgress;
class SkipAdButton;

// A mini player's video with Arc's controls over it, as the "Controls on hover"
// section of docs/plans/mini-player.md says: they show while the pointer is
// over the video. Holding on the video plays it at 2× until it is let go.
class MiniPlayerView : public views::View {
  METADATA_HEADER(MiniPlayerView, views::View)

 public:
  // The buttons a site offers or takes away.
  enum class SiteButton {
    kPlayPause,
    kSkipAd,
    kNextTrack,
    kPreviousTrack,
    // A presentation's slides share the track buttons.
    kNextSlide,
    kPreviousSlide,
    kMicrophone,
    kCamera,
    kHangUp,
  };

  // The ids of the controls, for tests.
  enum ViewId {
    kCloseButton = 1,
    kBackToTabButton,
    kReplayButton,
    kPlayPauseButton,
    kForwardButton,
    kMuteButton,
    kProgressBar,
    kTime,
    kFastLabel,
    kSkipAdButton,
    kPreviousTrackButton,
    kNextTrackButton,
    kMicrophoneButton,
    kCameraButton,
    kHangUpButton,
  };

  // Sizes of the controls: docked, or in the larger floating window.
  struct Sizes;

  // How long the video must be held still before it plays at 2×.
  static constexpr base::TimeDelta kHoldDelay = base::Milliseconds(300);

  // How glassy the controls are, from 0, flat, to 100.
  using GlassOf = base::RepeatingCallback<int()>;

  MiniPlayerView(content::VideoPictureInPictureWindowController* controller,
                 GlassOf glass);
  MiniPlayerView(const MiniPlayerView&) = delete;
  MiniPlayerView& operator=(const MiniPlayerView&) = delete;
  ~MiniPlayerView() override;

  void SetSurfaceId(const viz::SurfaceId& surface_id);
  void SetNaturalSize(const gfx::Size& natural_size);
  const gfx::Size& natural_size() const { return natural_size_; }
  void SetPlaybackState(content::VideoOverlayWindow::PlaybackState state);
  void SetMediaPosition(const media_session::MediaPosition& position);
  void SetMuted(bool muted);
  void SetMicrophoneMuted(bool muted);
  void SetCameraOn(bool on);
  void SetSiteButtonVisible(SiteButton button, bool visible);
  // Without controls, as for a camera stream, the video shows alone.
  void SetControlsAllowed(bool allowed);

  // While on, dragging the video moves the view's widget, as the floating
  // mini player does.
  void set_moves_widget(bool moves_widget) { moves_widget_ = moves_widget; }

  // Rounds the corners to `radius`, with the floating mini player's lit edge
  // when `lit_rim`.
  void SetCorners(float radius, bool lit_rim);

  // Shows or hides the controls, as the pointer coming and going does.
  void ShowControls(bool show);
  bool controls_shown() const { return controls_shown_; }

  // True while the video plays at 2× because it is held.
  bool fast() const { return fast_; }

  // Where the video is drawn, in the view's coordinates.
  gfx::Rect GetVideoBounds() const;

  // Called when the video's place in the view changes, so the controller can
  // tell the page what size it is shown at.
  void set_on_video_bounds_changed(base::RepeatingClosure callback) {
    on_video_bounds_changed_ = std::move(callback);
  }

  // Stops showing the video in the widget it is in, before that widget goes.
  void DetachFromCompositor();

  // Starts and ends 2× as holding the video does, for tests that cannot hold
  // a real mouse button.
  void HoldForTesting(bool hold);

  // views::View:
  void Layout(PassKey) override;
  void OnThemeChanged() override;
  bool OnMousePressed(const ui::MouseEvent& event) override;
  bool OnMouseDragged(const ui::MouseEvent& event) override;
  void OnMouseReleased(const ui::MouseEvent& event) override;
  void OnMouseCaptureLost() override;
  void OnMouseEntered(const ui::MouseEvent& event) override;
  void OnMouseMoved(const ui::MouseEvent& event) override;
  void OnMouseExited(const ui::MouseEvent& event) override;
  bool OnKeyPressed(const ui::KeyEvent& event) override;
  void AddedToWidget() override;
  void RemovedFromWidget() override;

 private:
  const Sizes& sizes() const;

  void TogglePlayPause();
  void NextTrackOrSlide();
  void PreviousTrackOrSlide();
  void SeekBy(base::TimeDelta delta);
  void SeekTo(base::TimeDelta time);
  void ToggleMute();
  void UpdateTime();
  void UpdateControlsVisibility();
  void SetFast(bool fast);
  void RegisterFrameSink();
  void UnregisterFrameSink();

  const raw_ptr<content::VideoPictureInPictureWindowController> controller_;
  const GlassOf glass_;

  raw_ptr<views::View> video_ = nullptr;
  raw_ptr<views::View> controls_ = nullptr;
  raw_ptr<MiniPlayerButton> close_ = nullptr;
  raw_ptr<MiniPlayerButton> back_to_tab_ = nullptr;
  raw_ptr<MiniPlayerButton> replay_ = nullptr;
  raw_ptr<MiniPlayerButton> play_pause_ = nullptr;
  raw_ptr<MiniPlayerButton> forward_ = nullptr;
  raw_ptr<MiniPlayerButton> previous_track_ = nullptr;
  raw_ptr<MiniPlayerButton> next_track_ = nullptr;
  raw_ptr<MiniPlayerButton> microphone_ = nullptr;
  raw_ptr<MiniPlayerButton> camera_ = nullptr;
  raw_ptr<MiniPlayerButton> hang_up_ = nullptr;
  raw_ptr<MiniPlayerButton> mute_ = nullptr;
  raw_ptr<SkipAdButton> skip_ad_ = nullptr;
  raw_ptr<views::Label> time_ = nullptr;
  raw_ptr<MiniPlayerProgress> progress_ = nullptr;
  raw_ptr<FastLabel> fast_label_ = nullptr;
  raw_ptr<views::View> rim_ = nullptr;

  gfx::Size natural_size_;
  std::optional<viz::SurfaceId> surface_id_;
  // The compositor the video is shown in, while it is.
  raw_ptr<ui::Compositor> frame_sink_compositor_ = nullptr;
  media_session::MediaPosition position_;
  content::VideoOverlayWindow::PlaybackState playback_state_ =
      content::VideoOverlayWindow::kPaused;
  bool controls_allowed_ = true;
  // Which the site offers: tracks, slides, or both.
  bool has_next_track_ = false;
  bool has_next_slide_ = false;
  bool has_previous_track_ = false;
  bool has_previous_slide_ = false;
  bool controls_shown_ = false;
  bool moves_widget_ = false;

  // A press on the video: where it was on the screen, and where the widget
  // was then. It becomes a drag once the pointer moves, or 2× once the hold
  // timer fires first.
  std::optional<gfx::Point> press_in_screen_;
  gfx::Point widget_origin_at_press_;
  bool dragging_ = false;
  bool fast_ = false;
  base::OneShotTimer hold_timer_;

  // Hides the controls a moment after the pointer leaves.
  base::OneShotTimer hide_timer_;
  // Moves the time and the progress bar along while the video plays.
  base::RepeatingTimer tick_;

  base::RepeatingClosure on_video_bounds_changed_;
};

}  // namespace uw

#endif  // UW_BROWSER_UI_VIEWS_MINI_PLAYER_MINI_PLAYER_VIEW_H_
