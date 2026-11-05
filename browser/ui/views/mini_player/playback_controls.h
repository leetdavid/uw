// Copyright 2026 The uw Authors. All rights reserved.

#ifndef UW_BROWSER_UI_VIEWS_MINI_PLAYER_PLAYBACK_CONTROLS_H_
#define UW_BROWSER_UI_VIEWS_MINI_PLAYER_PLAYBACK_CONTROLS_H_

#include <optional>
#include <string>
#include <vector>

#include "base/callback_list.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/scoped_observation.h"
#include "chrome/browser/picture_in_picture/picture_in_picture_window_manager.h"
#include "chrome/browser/ui/tabs/tab_strip_model_observer.h"
#include "components/prefs/pref_change_registrar.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "services/media_session/public/mojom/audio_focus.mojom.h"
#include "services/media_session/public/mojom/media_session.mojom.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/gfx/image/image_skia.h"
#include "ui/views/controls/button/button.h"

class BrowserWindowInterface;

namespace content {
class WebContents;
}

namespace views {
class ImageButton;
}

namespace uw {

class MiniPlayerPlacement;

// The sidebar's playback controls, per "Playback controls" and "In other
// windows" in docs/plans/mini-player.md: one row above the foot for the
// window's tab that started playing sound last, while the user is on another
// tab. Its art, title, and where the sound comes from, with play or pause, and
// previous and next when the site offers them. A click goes to the tab. Sound
// in a mini player or a site's own window has its own controls, so it gets no
// such row. Two more rows above it show the mini player and a live window
// while they dock in another window, with Move here; a click goes to that
// window.
class PlaybackControls : public views::Button,
                         public TabStripModelObserver,
                         public PictureInPictureWindowManager::Observer,
                         public media_session::mojom::AudioFocusObserver,
                         public media_session::mojom::MediaSessionObserver {
  METADATA_HEADER(PlaybackControls, views::Button)

 public:
  static constexpr int kHeight = 44;

  // What a row is for.
  enum class Kind {
    // The window's own tab that started playing sound last.
    kOwnTabs,
    // The mini player, while its home window is another.
    kMiniPlayer,
    // A site's live window, while its home window is another.
    kLiveWindow,
  };

  PlaybackControls(BrowserWindowInterface* window, Kind kind);
  PlaybackControls(const PlaybackControls&) = delete;
  PlaybackControls& operator=(const PlaybackControls&) = delete;
  ~PlaybackControls() override;

  Kind kind() const { return kind_; }
  // The tab the row is for, shown or not.
  content::WebContents* tab() { return tab_.get(); }
  bool playing() const { return playing_; }
  views::ImageButton* previous() { return previous_; }
  views::ImageButton* play_pause() { return play_pause_; }
  views::ImageButton* next() { return next_; }
  views::ImageButton* move_here() { return move_here_; }
  // The × on the row's corner, shown under the pointer.
  views::ImageButton* dismiss() { return dismiss_; }
  bool has_art() const { return !art_.isNull(); }
  void SetArtForTesting(const SkBitmap& art);

  // views::Button:
  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override;
  void Layout(PassKey) override;
  void OnThemeChanged() override;
  void StateChanged(ButtonState old_state) override;
  void PaintButtonContents(gfx::Canvas* canvas) override;

  // TabStripModelObserver:
  void OnTabStripModelChanged(
      TabStripModel* tab_strip_model,
      const TabStripModelChange& change,
      const TabStripSelectionChange& selection) override;

  // PictureInPictureWindowManager::Observer:
  void OnEnterPictureInPicture() override;
  void OnExitPictureInPicture() override;

  // media_session::mojom::AudioFocusObserver:
  void OnFocusGained(
      media_session::mojom::AudioFocusRequestStatePtr session) override;
  void OnFocusLost(
      media_session::mojom::AudioFocusRequestStatePtr session) override {}
  void OnRequestIdReleased(const base::UnguessableToken& request_id) override {}

  // media_session::mojom::MediaSessionObserver:
  void MediaSessionInfoChanged(
      media_session::mojom::MediaSessionInfoPtr session_info) override;
  void MediaSessionMetadataChanged(
      const std::optional<media_session::MediaMetadata>& metadata) override;
  void MediaSessionActionsChanged(
      const std::vector<media_session::mojom::MediaSessionAction>& actions)
      override;
  void MediaSessionImagesChanged(
      const base::flat_map<media_session::mojom::MediaSessionImageType,
                           std::vector<media_session::MediaImage>>& images)
      override;
  void MediaSessionPositionChanged(
      const std::optional<media_session::MediaPosition>& position) override {}

 private:
  // Follows `contents`, a tab of this window that started playing, or the tab
  // of the mini player or live window the row is for.
  void Follow(content::WebContents* contents);
  // Follows the mini player or live window while it docks in another window,
  // and nothing otherwise.
  void UpdateElsewhere();
  // Posts UpdateElsewhere(), for changes that settle within the task.
  void UpdateElsewhereSoon();
  // The placement of what the row is for, outside kOwnTabs.
  MiniPlayerPlacement* Placement();
  // Shows the row while its tab is not the one in front and its sound is not
  // in a mini player.
  void UpdateVisibility();
  void UpdateButtons();
  void OnArt(const SkBitmap& art);
  // Goes to the tab, or to the home window outside kOwnTabs.
  void GoToTab();
  void PlayOrPause();
  void MoveHere();
  // Pauses the tab's sound and hides the row until that tab plays again or
  // another tab starts playing. Outside kOwnTabs, closes the mini player or
  // live window.
  void Dismiss();
  SkColor TextColor() const;

  const raw_ptr<BrowserWindowInterface> window_;
  const Kind kind_;
  base::WeakPtr<content::WebContents> tab_;
  bool playing_ = false;
  bool has_previous_ = false;
  bool has_next_ = false;
  bool has_play_pause_ = false;
  bool dismissed_ = false;
  std::u16string title_;
  std::u16string source_;
  gfx::ImageSkia art_;
  raw_ptr<views::ImageButton> previous_ = nullptr;
  raw_ptr<views::ImageButton> play_pause_ = nullptr;
  raw_ptr<views::ImageButton> next_ = nullptr;
  raw_ptr<views::ImageButton> move_here_ = nullptr;
  raw_ptr<views::ImageButton> dismiss_ = nullptr;
  mojo::Receiver<media_session::mojom::AudioFocusObserver> focus_{this};
  mojo::Receiver<media_session::mojom::MediaSessionObserver> session_{this};
  base::ScopedObservation<PictureInPictureWindowManager,
                          PictureInPictureWindowManager::Observer>
      picture_in_picture_{this};
  base::CallbackListSubscription stack_;
  base::CallbackListSubscription homes_;
  PrefChangeRegistrar prefs_;
  base::WeakPtrFactory<PlaybackControls> weak_factory_{this};
};

}  // namespace uw

#endif  // UW_BROWSER_UI_VIEWS_MINI_PLAYER_PLAYBACK_CONTROLS_H_
