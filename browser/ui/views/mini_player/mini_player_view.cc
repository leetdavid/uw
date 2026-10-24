// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/browser/ui/views/mini_player/mini_player_view.h"

#include <algorithm>
#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "base/strings/strcat.h"
#include "base/strings/stringprintf.h"
#include "base/strings/utf_string_conversions.h"
#include "cc/layers/deadline_policy.h"
#include "chrome/browser/picture_in_picture/picture_in_picture_window_manager.h"
#include "chrome/grit/generated_resources.h"
#include "components/vector_icons/vector_icons.h"
#include "content/public/browser/video_picture_in_picture_window_controller.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/compositor/compositor.h"
#include "ui/compositor/layer.h"
#include "ui/compositor/scoped_layer_animation_settings.h"
#include "ui/events/event.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/gfx/font_list.h"
#include "ui/gfx/geometry/rounded_corners_f.h"
#include "ui/gfx/shadow_value.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/controls/label.h"
#include "ui/views/view_shadow.h"
#include "ui/views/widget/widget.h"
#include "uw/browser/ui/vector_icons/vector_icons.h"
#include "uw/browser/ui/views/mini_player/mini_player_controls.h"
#include "uw/browser/ui/views/mini_player/playback_speed.h"

namespace uw {
namespace {

constexpr base::TimeDelta kSeekStep = base::Seconds(10);
constexpr base::TimeDelta kFadeIn = base::Milliseconds(150);
constexpr base::TimeDelta kFadeOut = base::Milliseconds(200);
constexpr base::TimeDelta kHideAfterLeaving = base::Seconds(1);
constexpr base::TimeDelta kTick = base::Milliseconds(250);

// Below this width the mini player uses the docked sizes.
constexpr int kFloatingSizesFrom = 320;

constexpr int kFloatingCornerRadius = 12;
constexpr int kDockedCornerRadius = 10;
constexpr int kDockedShadowElevation = 4;

// "12:41" or "1:02:41".
std::u16string FormatTime(base::TimeDelta time) {
  const int64_t seconds = std::max<int64_t>(0, time.InSeconds());
  const int64_t hours = seconds / 3600;
  const int64_t minutes = seconds / 60 % 60;
  if (hours > 0) {
    return base::UTF8ToUTF16(base::StringPrintf(
        "%lld:%02lld:%02lld", static_cast<long long>(hours),
        static_cast<long long>(minutes), static_cast<long long>(seconds % 60)));
  }
  return base::UTF8ToUTF16(
      base::StringPrintf("%lld:%02lld", static_cast<long long>(minutes),
                         static_cast<long long>(seconds % 60)));
}

bool Seekable(const media_session::MediaPosition& position) {
  return position.duration().is_positive() && !position.duration().is_max();
}

}  // namespace

struct MiniPlayerView::Sizes {
  int corner;
  int corner_inset;
  int corner_icon;
  int seek;
  int seek_icon;
  int play;
  int play_icon;
  int gap;
  int mute;
  int mute_icon;
  int time_font;
  int bar_height;
  int bar_inset;
  int bar_bottom;
  int knob;
  // From the video's bottom edge up to the bottom of the time and mute row.
  int bottom_row;
  int pill;
  int pill_top;
};

namespace {

// The frames' docked and floating sizes.
constexpr MiniPlayerView::Sizes kDocked{24, 8,  14, 28, 17, 38, 16, 14, 22,
                                        13, 11, 3,  10, 9,  9,  17, 22, 9};
constexpr MiniPlayerView::Sizes kFloating{30, 12, 16, 38, 22, 52, 22, 22, 28,
                                          15, 12, 4,  14, 13, 11, 24, 26, 12};

}  // namespace

MiniPlayerView::MiniPlayerView(
    content::VideoPictureInPictureWindowController* controller,
    GlassOf glass)
    : controller_(controller), glass_(std::move(glass)) {
  SetPaintToLayer();
  layer()->SetFillsBoundsOpaquely(false);
  layer()->SetMasksToBounds(true);
  layer()->SetName("MiniPlayer");
  SetNotifyEnterExitOnChild(true);
  SetFocusBehavior(FocusBehavior::ALWAYS);
  GetViewAccessibility().SetRole(ax::mojom::Role::kGroup);
  GetViewAccessibility().SetName(
      l10n_util::GetStringUTF16(IDS_PICTURE_IN_PICTURE_TITLE_TEXT));

  video_ = AddChildView(std::make_unique<views::View>());
  video_->SetPaintToLayer(ui::LAYER_SURFACE);
  video_->layer()->SetMasksToBounds(true);
  video_->layer()->SetFillsBoundsOpaquely(false);
  video_->layer()->SetName("MiniPlayerVideo");
  video_->SetCanProcessEventsWithinSubtree(false);

  controls_ = AddChildView(std::make_unique<views::View>());
  auto* scrim = controls_->AddChildView(std::make_unique<ControlsScrim>());
  scrim->SetPaintToLayer();
  scrim->layer()->SetFillsBoundsOpaquely(false);

  auto button = [&](views::Button::PressedCallback callback,
                    const gfx::VectorIcon& icon, int message, ViewId id) {
    auto* added = controls_->AddChildView(std::make_unique<MiniPlayerButton>(
        std::move(callback), icon, l10n_util::GetStringUTF16(message), glass_));
    added->SetID(id);
    return added;
  };
  close_ = button(base::BindRepeating(
                      [](MiniPlayerView* view) {
                        PictureInPictureWindowManager::GetInstance()
                            ->ExitPictureInPictureViaWindowUi(
                                view->play_pause_->GetVisible()
                                    ? PictureInPictureWindowManager::
                                          UiBehavior::kCloseWindowAndPauseVideo
                                    : PictureInPictureWindowManager::
                                          UiBehavior::kCloseWindowOnly);
                      },
                      base::Unretained(this)),
                  kMiniPlayerCloseIcon,
                  IDS_PICTURE_IN_PICTURE_CLOSE_CONTROL_TEXT, kCloseButton);
  back_to_tab_ =
      button(base::BindRepeating([] {
               PictureInPictureWindowManager::GetInstance()
                   ->ExitPictureInPictureViaWindowUi(
                       PictureInPictureWindowManager::UiBehavior::
                           kCloseWindowAndFocusOpener);
             }),
             kMiniPlayerBackToTabIcon,
             IDS_PICTURE_IN_PICTURE_BACK_TO_TAB_CONTROL_TEXT, kBackToTabButton);
  previous_track_ =
      button(base::BindRepeating(&MiniPlayerView::PreviousTrackOrSlide,
                                 base::Unretained(this)),
             vector_icons::kSkipPreviousIcon,
             IDS_PICTURE_IN_PICTURE_PREVIOUS_TRACK_CONTROL_ACCESSIBLE_TEXT,
             kPreviousTrackButton);
  replay_ = button(base::BindRepeating(&MiniPlayerView::SeekBy,
                                       base::Unretained(this), -kSeekStep),
                   kMiniPlayerReplayIcon, IDS_PICTURE_IN_PICTURE_REPLAY_10_TEXT,
                   kReplayButton);
  replay_->set_seconds(u"10");
  play_pause_ =
      button(base::BindRepeating(&MiniPlayerView::TogglePlayPause,
                                 base::Unretained(this)),
             kMiniPlayerPlayIcon, IDS_PICTURE_IN_PICTURE_PLAY_CONTROL_TEXT,
             kPlayPauseButton);
  forward_ = button(base::BindRepeating(&MiniPlayerView::SeekBy,
                                        base::Unretained(this), kSeekStep),
                    kMiniPlayerForwardIcon,
                    IDS_PICTURE_IN_PICTURE_FORWARD_10_TEXT, kForwardButton);
  forward_->set_seconds(u"10");
  next_track_ =
      button(base::BindRepeating(&MiniPlayerView::NextTrackOrSlide,
                                 base::Unretained(this)),
             vector_icons::kSkipNextIcon,
             IDS_PICTURE_IN_PICTURE_NEXT_TRACK_CONTROL_ACCESSIBLE_TEXT,
             kNextTrackButton);
  microphone_ = button(
      base::BindRepeating(
          &content::VideoPictureInPictureWindowController::ToggleMicrophone,
          base::Unretained(controller_)),
      vector_icons::kMicIcon, IDS_PICTURE_IN_PICTURE_MUTE_MICROPHONE_TEXT,
      kMicrophoneButton);
  camera_ =
      button(base::BindRepeating(
                 &content::VideoPictureInPictureWindowController::ToggleCamera,
                 base::Unretained(controller_)),
             vector_icons::kVideocamIcon,
             IDS_PICTURE_IN_PICTURE_TURN_OFF_CAMERA_TEXT, kCameraButton);
  hang_up_ = button(base::BindRepeating(
                        &content::VideoPictureInPictureWindowController::HangUp,
                        base::Unretained(controller_)),
                    vector_icons::kCallEndFilledIcon,
                    IDS_PICTURE_IN_PICTURE_HANG_UP_TEXT, kHangUpButton);
  hang_up_->set_fill(SkColorSetRGB(0xD9, 0x30, 0x25));
  mute_ = button(
      base::BindRepeating(&MiniPlayerView::ToggleMute, base::Unretained(this)),
      kMiniPlayerVolumeIcon, IDS_PICTURE_IN_PICTURE_MUTE_TEXT, kMuteButton);

  skip_ad_ = controls_->AddChildView(std::make_unique<SkipAdButton>(
      base::BindRepeating(
          &content::VideoPictureInPictureWindowController::SkipAd,
          base::Unretained(controller_)),
      glass_));
  skip_ad_->SetID(kSkipAdButton);

  time_ = controls_->AddChildView(std::make_unique<views::Label>());
  time_->SetID(kTime);
  time_->SetPaintToLayer();
  time_->layer()->SetFillsBoundsOpaquely(false);
  time_->SetAutoColorReadabilityEnabled(false);
  time_->SetEnabledColor(SK_ColorWHITE);
  time_->SetBackgroundColor(SK_ColorTRANSPARENT);
  time_->SetSubpixelRenderingEnabled(false);
  time_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  time_->SetShadows(
      {gfx::ShadowValue({0, 1}, 2, SkColorSetA(SK_ColorBLACK, 0x66))});

  progress_ = controls_->AddChildView(
      std::make_unique<MiniPlayerProgress>(base::BindRepeating(
          [](MiniPlayerView* view, double progress) {
            view->SeekTo(view->position_.duration() * progress);
          },
          base::Unretained(this))));
  progress_->SetID(kProgressBar);
  progress_->SetPaintToLayer();
  progress_->layer()->SetFillsBoundsOpaquely(false);

  auto fast_label = std::make_unique<FastLabel>(glass_);
  fast_label->SetID(kFastLabel);
  fast_label->SetVisible(false);
  fast_label_ = AddChildView(std::move(fast_label));

  for (views::View* site : {previous_track_.get(), next_track_.get(),
                            microphone_.get(), camera_.get(), hang_up_.get()}) {
    site->SetVisible(false);
  }
  skip_ad_->SetVisible(false);
  play_pause_->SetVisible(false);
  ShowControls(false);
}

MiniPlayerView::~MiniPlayerView() {
  shadow_.reset();
  UnregisterFrameSink();
}

const MiniPlayerView::Sizes& MiniPlayerView::sizes() const {
  return GetVideoBounds().width() >= kFloatingSizesFrom ? kFloating : kDocked;
}

void MiniPlayerView::SetSurfaceId(const viz::SurfaceId& surface_id) {
  UnregisterFrameSink();
  surface_id_ = surface_id;
  RegisterFrameSink();
  auto* surface = video_->layer()->AsSurface();
  surface->SetBackgroundColor(SkColors::kBlack);
  surface->SetShowSurface(surface_id, GetVideoBounds().size(),
                          cc::DeadlinePolicy::UseDefaultDeadline(),
                          /*stretch_content_to_fill_bounds=*/true);
}

void MiniPlayerView::SetNaturalSize(const gfx::Size& natural_size) {
  natural_size_ = natural_size;
  InvalidateLayout();
}

void MiniPlayerView::SetPlaybackState(
    content::VideoOverlayWindow::PlaybackState state) {
  playback_state_ = state;
  const bool playing = state == content::VideoOverlayWindow::kPlaying;
  play_pause_->SetIcon(playing ? kMiniPlayerPauseIcon : kMiniPlayerPlayIcon,
                       l10n_util::GetStringUTF16(
                           playing ? IDS_PICTURE_IN_PICTURE_PAUSE_CONTROL_TEXT
                                   : IDS_PICTURE_IN_PICTURE_PLAY_CONTROL_TEXT));
  UpdateControlsVisibility();
}

void MiniPlayerView::SetMediaPosition(
    const media_session::MediaPosition& position) {
  position_ = position;
  UpdateTime();
  UpdateControlsVisibility();
}

void MiniPlayerView::SetMuted(bool muted) {
  mute_->SetIcon(
      muted ? kMiniPlayerMutedIcon : kMiniPlayerVolumeIcon,
      l10n_util::GetStringUTF16(muted ? IDS_PICTURE_IN_PICTURE_UNMUTE_TEXT
                                      : IDS_PICTURE_IN_PICTURE_MUTE_TEXT));
}

void MiniPlayerView::SetMicrophoneMuted(bool muted) {
  microphone_->SetIcon(
      muted ? vector_icons::kMicOffIcon : vector_icons::kMicIcon,
      l10n_util::GetStringUTF16(
          muted ? IDS_PICTURE_IN_PICTURE_UNMUTE_MICROPHONE_TEXT
                : IDS_PICTURE_IN_PICTURE_MUTE_MICROPHONE_TEXT));
}

void MiniPlayerView::SetCameraOn(bool on) {
  camera_->SetIcon(
      on ? vector_icons::kVideocamIcon : vector_icons::kVideocamOffIcon,
      l10n_util::GetStringUTF16(
          on ? IDS_PICTURE_IN_PICTURE_TURN_OFF_CAMERA_TEXT
             : IDS_PICTURE_IN_PICTURE_TURN_ON_CAMERA_TEXT));
}

void MiniPlayerView::SetSiteButtonVisible(SiteButton button, bool visible) {
  views::View* view = nullptr;
  switch (button) {
    case SiteButton::kNextSlide:
      has_next_slide_ = visible;
      view = next_track_;
      visible = has_next_track_ || has_next_slide_;
      break;
    case SiteButton::kPreviousSlide:
      has_previous_slide_ = visible;
      view = previous_track_;
      visible = has_previous_track_ || has_previous_slide_;
      break;
    case SiteButton::kPlayPause:
      view = play_pause_;
      break;
    case SiteButton::kSkipAd:
      view = skip_ad_;
      break;
    case SiteButton::kNextTrack:
      has_next_track_ = visible;
      view = next_track_;
      visible = has_next_track_ || has_next_slide_;
      break;
    case SiteButton::kPreviousTrack:
      has_previous_track_ = visible;
      view = previous_track_;
      visible = has_previous_track_ || has_previous_slide_;
      break;
    case SiteButton::kMicrophone:
      view = microphone_;
      break;
    case SiteButton::kCamera:
      view = camera_;
      break;
    case SiteButton::kHangUp:
      view = hang_up_;
      break;
  }
  view->SetVisible(visible);
  UpdateControlsVisibility();
}

void MiniPlayerView::SetControlsAllowed(bool allowed) {
  controls_allowed_ = allowed;
  UpdateControlsVisibility();
}

void MiniPlayerView::SetFloatingLook() {
  shadow_.reset();
  SetCorners(kFloatingCornerRadius, std::nullopt);
}

void MiniPlayerView::SetDockedLook(SkColor ring, bool shadow) {
  SetCorners(kDockedCornerRadius, ring);
  if (shadow && !shadow_) {
    shadow_ = std::make_unique<views::ViewShadow>(this, kDockedShadowElevation);
    shadow_->SetRoundedCornerRadius(kDockedCornerRadius);
  } else if (!shadow) {
    shadow_.reset();
  }
}

void MiniPlayerView::SetCorners(float radius, std::optional<SkColor> ring) {
  layer()->SetRoundedCornerRadius(gfx::RoundedCornersF(radius));
  layer()->SetIsFastRoundedCorner(true);
  if (rim_) {
    views::View* rim = rim_;
    rim_ = nullptr;
    RemoveChildViewT(rim);
  }
  rim_ = AddChildView(std::make_unique<Rim>(radius, ring));
  InvalidateLayout();
}

void MiniPlayerView::ShowControls(bool show) {
  hide_timer_.Stop();
  controls_shown_ = show;
  UpdateControlsVisibility();
}

gfx::Rect MiniPlayerView::GetVideoBounds() const {
  const gfx::Rect bounds = GetLocalBounds();
  if (natural_size_.IsEmpty() || bounds.IsEmpty()) {
    return bounds;
  }
  const double scale =
      std::min(static_cast<double>(bounds.width()) / natural_size_.width(),
               static_cast<double>(bounds.height()) / natural_size_.height());
  gfx::Size size(std::round(natural_size_.width() * scale),
                 std::round(natural_size_.height() * scale));
  // A pixel short of the bounds is rounding, not letterboxing.
  if (bounds.width() - size.width() <= 1) {
    size.set_width(bounds.width());
  }
  if (bounds.height() - size.height() <= 1) {
    size.set_height(bounds.height());
  }
  gfx::Rect video = bounds;
  video.ClampToCenteredSize(size);
  return video;
}

void MiniPlayerView::HoldForTesting(bool hold) {
  SetFast(hold);
}

void MiniPlayerView::Layout(PassKey) {
  const gfx::Rect video = GetVideoBounds();
  const bool video_moved = video != video_->bounds();
  video_->SetBoundsRect(video);
  if (video_->layer()->HasExternalContent()) {
    video_->layer()->AsSurface()->SetSurfaceSize(video.size());
  }
  controls_->SetBoundsRect(GetLocalBounds());
  controls_->children().front()->SetBoundsRect(video);

  const Sizes& s = sizes();
  close_->SetSizes(s.corner, s.corner_icon);
  close_->SetBounds(video.x() + s.corner_inset, video.y() + s.corner_inset,
                    s.corner, s.corner);
  back_to_tab_->SetSizes(s.corner, s.corner_icon);
  back_to_tab_->SetBounds(video.right() - s.corner_inset - s.corner,
                          video.y() + s.corner_inset, s.corner, s.corner);

  std::vector<MiniPlayerButton*> middle;
  for (MiniPlayerButton* button :
       {previous_track_.get(), replay_.get(), play_pause_.get(), forward_.get(),
        next_track_.get()}) {
    const bool play = button == play_pause_;
    button->SetSizes(play ? s.play : s.seek, play ? s.play_icon : s.seek_icon);
    if (button->GetVisible()) {
      middle.push_back(button);
    }
  }
  int width = 0;
  for (MiniPlayerButton* button : middle) {
    width += button->GetPreferredSize().width();
  }
  width += s.gap * std::max<int>(0, middle.size() - 1);
  int x = video.CenterPoint().x() - width / 2;
  for (MiniPlayerButton* button : middle) {
    const int size = button->GetPreferredSize().width();
    button->SetBounds(x, video.CenterPoint().y() - size / 2, size, size);
    x += size + s.gap;
  }

  // The bottom row: the time, then a call's buttons in the middle, then skip
  // ad and mute at the right.
  const int row_center = video.bottom() - s.bottom_row - s.mute / 2;
  mute_->SetSizes(s.mute, s.mute_icon);
  mute_->SetBounds(video.right() - s.corner_inset - s.mute,
                   row_center - s.mute / 2, s.mute, s.mute);
  skip_ad_->SetHeight(s.pill);
  const gfx::Size skip = skip_ad_->GetPreferredSize();
  skip_ad_->SetBounds(mute_->x() - 6 - skip.width(),
                      row_center - skip.height() / 2, skip.width(),
                      skip.height());
  std::vector<MiniPlayerButton*> call;
  for (MiniPlayerButton* button :
       {microphone_.get(), camera_.get(), hang_up_.get()}) {
    button->SetSizes(s.seek, s.corner_icon);
    if (button->GetVisible()) {
      call.push_back(button);
    }
  }
  x = video.CenterPoint().x() - (s.seek * static_cast<int>(call.size()) +
                                 12 * std::max<int>(0, call.size() - 1)) /
                                    2;
  for (MiniPlayerButton* button : call) {
    button->SetBounds(x, video.bottom() - s.bottom_row + s.mute / 2 - s.seek,
                      s.seek, s.seek);
    x += s.seek + 12;
  }

  const gfx::FontList& base = views::Label::GetDefaultFontList();
  time_->SetFontList(base.DeriveWithSizeDelta(s.time_font - base.GetFontSize())
                         .DeriveWithWeight(gfx::Font::Weight::MEDIUM));
  const int time_height = time_->GetPreferredSize().height();
  time_->SetBounds(video.x() + s.bar_inset, row_center - time_height / 2,
                   video.width() / 2, time_height);

  // The bar is drawn thin but takes presses across the knob's height.
  progress_->SetSizes(s.bar_height, s.knob);
  progress_->SetBounds(
      video.x() + s.bar_inset,
      video.bottom() - s.bar_bottom - s.bar_height / 2 - s.knob / 2 - 1,
      video.width() - 2 * s.bar_inset, s.knob + 2);

  fast_label_->SetHeight(s.pill + 2);
  const gfx::Size fast = fast_label_->GetPreferredSize();
  fast_label_->SetBounds(video.CenterPoint().x() - fast.width() / 2,
                         video.y() + s.pill_top, fast.width(), fast.height());

  if (rim_) {
    rim_->SetBoundsRect(GetLocalBounds());
  }

  if (video_moved && on_video_bounds_changed_) {
    on_video_bounds_changed_.Run();
  }
}

void MiniPlayerView::OnThemeChanged() {
  views::View::OnThemeChanged();
  UpdateControlsVisibility();
}

bool MiniPlayerView::OnMousePressed(const ui::MouseEvent& event) {
  if (!event.IsOnlyLeftMouseButton()) {
    return false;
  }
  RequestFocus();
  gfx::Point in_screen = event.location();
  views::View::ConvertPointToScreen(this, &in_screen);
  press_in_screen_ = in_screen;
  widget_origin_at_press_ = GetWidget()->GetWindowBoundsInScreen().origin();
  dragging_ = false;
  hold_timer_.Start(
      FROM_HERE, kHoldDelay,
      base::BindOnce(&MiniPlayerView::SetFast, base::Unretained(this), true));
  return true;
}

bool MiniPlayerView::OnMouseDragged(const ui::MouseEvent& event) {
  if (!press_in_screen_ || fast_) {
    return press_in_screen_.has_value();
  }
  gfx::Point in_screen = event.location();
  views::View::ConvertPointToScreen(this, &in_screen);
  const gfx::Vector2d moved = in_screen - *press_in_screen_;
  if (!dragging_ && !ExceededDragThreshold(moved)) {
    return true;
  }
  dragging_ = true;
  hold_timer_.Stop();
  if (moves_widget_) {
    GetWidget()->SetBounds(
        gfx::Rect(widget_origin_at_press_ + moved,
                  GetWidget()->GetWindowBoundsInScreen().size()));
  }
  return true;
}

void MiniPlayerView::OnMouseReleased(const ui::MouseEvent& event) {
  OnMouseCaptureLost();
}

void MiniPlayerView::OnMouseCaptureLost() {
  hold_timer_.Stop();
  press_in_screen_.reset();
  dragging_ = false;
  if (fast_) {
    SetFast(false);
  }
}

void MiniPlayerView::OnMouseEntered(const ui::MouseEvent& event) {
  ShowControls(true);
}

void MiniPlayerView::OnMouseMoved(const ui::MouseEvent& event) {
  if (!controls_shown_) {
    ShowControls(true);
  }
  hide_timer_.Stop();
}

void MiniPlayerView::OnMouseExited(const ui::MouseEvent& event) {
  if (press_in_screen_ || progress_->dragging()) {
    return;
  }
  hide_timer_.Start(FROM_HERE, kHideAfterLeaving,
                    base::BindOnce(&MiniPlayerView::ShowControls,
                                   base::Unretained(this), false));
}

bool MiniPlayerView::OnKeyPressed(const ui::KeyEvent& event) {
  switch (event.key_code()) {
    case ui::VKEY_SPACE:
      if (play_pause_->GetVisible()) {
        TogglePlayPause();
        return true;
      }
      return false;
    case ui::VKEY_LEFT:
      if (replay_->GetVisible()) {
        SeekBy(-kSeekStep);
        return true;
      }
      return false;
    case ui::VKEY_RIGHT:
      if (forward_->GetVisible()) {
        SeekBy(kSeekStep);
        return true;
      }
      return false;
    default:
      return false;
  }
}

void MiniPlayerView::AddedToWidget() {
  RegisterFrameSink();
}

void MiniPlayerView::RemovedFromWidget() {
  UnregisterFrameSink();
}

void MiniPlayerView::DetachFromCompositor() {
  UnregisterFrameSink();
}

void MiniPlayerView::TogglePlayPause() {
  SetPlaybackState(controller_->TogglePlayPause()
                       ? content::VideoOverlayWindow::kPlaying
                       : content::VideoOverlayWindow::kPaused);
}

void MiniPlayerView::NextTrackOrSlide() {
  if (has_next_track_) {
    controller_->NextTrack();
  } else {
    controller_->NextSlide();
  }
}

void MiniPlayerView::PreviousTrackOrSlide() {
  if (has_previous_track_) {
    controller_->PreviousTrack();
  } else {
    controller_->PreviousSlide();
  }
}

void MiniPlayerView::SeekBy(base::TimeDelta delta) {
  SeekTo(position_.GetPosition() + delta);
}

void MiniPlayerView::SeekTo(base::TimeDelta time) {
  if (!Seekable(position_)) {
    return;
  }
  controller_->SeekTo(
      std::clamp(time, base::TimeDelta(), position_.duration()));
}

void MiniPlayerView::ToggleMute() {
  const bool muted = !controller_->GetMuteStatus();
  controller_->RequestMute(muted);
  SetMuted(muted);
}

void MiniPlayerView::UpdateTime() {
  if (!Seekable(position_)) {
    return;
  }
  const base::TimeDelta now = std::clamp(
      position_.GetPosition(), base::TimeDelta(), position_.duration());
  time_->SetText(base::StrCat(
      {FormatTime(now), u" / ", FormatTime(position_.duration())}));
  progress_->SetProgress(now / position_.duration());
}

void MiniPlayerView::UpdateControlsVisibility() {
  const bool seekable = Seekable(position_) && !skip_ad_->GetVisible();
  replay_->SetVisible(play_pause_->GetVisible() && seekable);
  forward_->SetVisible(play_pause_->GetVisible() && seekable);
  time_->SetVisible(Seekable(position_));
  progress_->SetVisible(Seekable(position_));
  progress_->SetAd(skip_ad_->GetVisible());
  progress_->SetSeekable(!fast_);
  fast_label_->SetVisible(fast_);

  const bool shown = controls_allowed_ && controls_shown_ && !fast_;
  controls_->SetCanProcessEventsWithinSubtree(shown);
  for (views::View* child : controls_->children()) {
    if (auto* button = views::AsViewClass<MiniPlayerButton>(child)) {
      button->UpdateGlass();
    }
    // While held at 2×, only the bar stays.
    const bool show = shown || (fast_ && child == progress_);
    if (!child->layer() || (child->layer()->GetTargetOpacity() == 1) == show) {
      continue;
    }
    ui::ScopedLayerAnimationSettings settings(child->layer()->GetAnimator());
    settings.SetTransitionDuration(show ? kFadeIn : kFadeOut);
    child->layer()->SetOpacity(show ? 1 : 0);
  }
  skip_ad_->UpdateGlass();
  fast_label_->UpdateGlass();

  if (shown && playback_state_ == content::VideoOverlayWindow::kPlaying) {
    if (!tick_.IsRunning()) {
      tick_.Start(FROM_HERE, kTick,
                  base::BindRepeating(&MiniPlayerView::UpdateTime,
                                      base::Unretained(this)));
    }
  } else {
    tick_.Stop();
  }
  InvalidateLayout();
}

void MiniPlayerView::SetFast(bool fast) {
  if (fast_ == fast) {
    return;
  }
  fast_ = fast;
  SetPlaybackFast(controller_->GetWebContents(), fast);
  UpdateControlsVisibility();
}

void MiniPlayerView::RegisterFrameSink() {
  if (frame_sink_compositor_ || !surface_id_ || !GetWidget() ||
      !GetWidget()->GetCompositor()) {
    return;
  }
  frame_sink_compositor_ = GetWidget()->GetCompositor();
  frame_sink_compositor_->AddChildFrameSink(surface_id_->frame_sink_id());
}

void MiniPlayerView::UnregisterFrameSink() {
  if (!frame_sink_compositor_) {
    return;
  }
  frame_sink_compositor_->RemoveChildFrameSink(surface_id_->frame_sink_id());
  frame_sink_compositor_ = nullptr;
}

BEGIN_METADATA(MiniPlayerView)
END_METADATA

}  // namespace uw
