// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/browser/ui/views/mini_player/mini_player.h"

#include <algorithm>
#include <optional>
#include <utility>

#include "base/functional/bind.h"
#include "base/memory/ptr_util.h"
#include "chrome/browser/picture_in_picture/picture_in_picture_occlusion_tracker.h"
#include "chrome/browser/picture_in_picture/picture_in_picture_widget_fade_animator.h"
#include "chrome/browser/picture_in_picture/picture_in_picture_window_manager.h"
#include "chrome/browser/ui/browser_window/public/global_browser_collection.h"
#include "chrome/grit/generated_resources.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/video_picture_in_picture_window_controller.h"
#include "content/public/browser/web_contents.h"
#include "ui/base/hit_test.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/display/display.h"
#include "ui/display/screen.h"
#include "ui/views/widget/widget_delegate.h"
#include "ui/views/window/frame_view.h"
#include "uw/browser/ui/glass/glass.h"
#include "uw/browser/ui/views/mini_player/mini_player_dock.h"
#include "uw/browser/ui/views/mini_player/mini_player_placement.h"
#include "uw/browser/ui/views/mini_player/mini_player_stack.h"
#include "uw/browser/ui/views/mini_player/mini_player_view.h"

#if BUILDFLAG(IS_MAC)
#include "uw/browser/ui/views/mini_player/floating_native_widget_mac.h"
#endif

namespace uw {
namespace {

// Chromium's smallest picture-in-picture window.
constexpr gfx::Size kMinimumSize(284, 160);

// The edge that resizes the window.
constexpr int kResizeBorder = 6;

// Where the user last left the floating mini player, for the next one.
std::optional<gfx::Rect>& LastBounds() {
  static std::optional<gfx::Rect> bounds;
  return bounds;
}

// The Glass slider of the last active window's workspace, or of the video's
// own window before any window was active.
int GlassOfLastActiveWindow(
    content::VideoPictureInPictureWindowController* controller) {
  BrowserWindowInterface* window =
      GlobalBrowserCollection::GetInstance()->GetLastActiveBrowser();
  if (!window) {
    tabs::TabInterface* tab =
        tabs::TabInterface::MaybeGetFromContents(controller->GetWebContents());
    window = tab ? tab->GetBrowserWindowInterface() : nullptr;
  }
  return window ? ElementGlassOfWindow(window) : 0;
}

// The frame of the floating mini player: none, only edges that resize it. The
// video takes every other press, so it can be held or dragged.
class MiniPlayerFrame : public views::FrameView {
  METADATA_HEADER(MiniPlayerFrame, views::FrameView)

 public:
  MiniPlayerFrame() = default;
  MiniPlayerFrame(const MiniPlayerFrame&) = delete;
  MiniPlayerFrame& operator=(const MiniPlayerFrame&) = delete;
  ~MiniPlayerFrame() override = default;

  // views::FrameView:
  gfx::Rect GetBoundsForClientView() const override { return bounds(); }
  gfx::Rect GetWindowBoundsForClientBounds(
      const gfx::Rect& client_bounds) const override {
    return bounds();
  }
  int NonClientHitTest(const gfx::Point& point) override {
    if (!bounds().Contains(point)) {
      return HTNOWHERE;
    }
    const int edge =
        GetHTComponentForFrame(point, gfx::Insets(kResizeBorder), 16, 16,
                               GetWidget()->widget_delegate()->CanResize());
    return edge == HTNOWHERE ? HTCLIENT : edge;
  }
};

BEGIN_METADATA(MiniPlayerFrame)
END_METADATA

}  // namespace

namespace internal {

MiniPlayerDelegateHolder::MiniPlayerDelegateHolder()
    : delegate(std::make_unique<views::WidgetDelegate>()) {
  delegate->SetCanResize(true);
  delegate->SetModalType(ui::mojom::ModalType::kNone);
  delegate->SetShowTitle(false);
  delegate->SetTitle(IDS_PICTURE_IN_PICTURE_TITLE_TEXT);
  delegate->SetFrameViewFactory(base::BindRepeating(
      [](views::Widget*) -> std::unique_ptr<views::FrameView> {
        return std::make_unique<MiniPlayerFrame>();
      }));
}

MiniPlayerDelegateHolder::~MiniPlayerDelegateHolder() = default;

}  // namespace internal

// static
std::unique_ptr<content::VideoOverlayWindow> MiniPlayer::Create(
    content::VideoPictureInPictureWindowController* controller) {
  auto mini_player = base::WrapUnique(new MiniPlayer(controller));
  mini_player->SetColorModeOverride(ui::ColorProviderKey::ColorMode::kDark);

  views::Widget::InitParams params(
      views::Widget::InitParams::WIDGET_OWNS_NATIVE_WIDGET,
      views::Widget::InitParams::TYPE_WINDOW);
  params.bounds = gfx::Rect(kMinimumSize);
  params.z_order = ui::ZOrderLevel::kFloatingWindow;
  params.visible_on_all_workspaces = true;
  params.remove_standard_frame = true;
  params.name = "MiniPlayer";
  params.layer_type = ui::LAYER_NOT_DRAWN;
  params.opacity = views::Widget::InitParams::WindowOpacity::kTranslucent;
  params.shadow_type = views::Widget::InitParams::ShadowType::kDrop;
  params.delegate = mini_player->delegate.get();
#if BUILDFLAG(IS_MAC)
  params.native_widget = new FloatingNativeWidgetMac(mini_player.get());
#endif

  // The window's client area holds the video. It stays when the video goes
  // elsewhere.
  auto* client =
      mini_player->delegate->SetContentsView(std::make_unique<views::View>());
  client->SetUseDefaultFillLayout(true);
  auto view = std::make_unique<MiniPlayerView>(
      controller, base::BindRepeating(&GlassOfLastActiveWindow,
                                      base::Unretained(controller)));
  view->SetFloatingLook();
  view->set_moves_widget(true);
  view->set_on_video_bounds_changed(base::BindRepeating(
      &content::VideoPictureInPictureWindowController::UpdateLayerBounds,
      base::Unretained(controller)));
  mini_player->view_ = client->AddChildView(std::move(view));
  mini_player->client_ = client;
  mini_player->Init(std::move(params));
  mini_player->SetBounds(mini_player->CalculateBounds());
  mini_player->placement_ = std::make_unique<MiniPlayerPlacement>(
      mini_player.get(),
      base::BindRepeating(&MiniPlayer::Place,
                          base::Unretained(mini_player.get())));

  if (PictureInPictureOcclusionTracker* tracker =
          PictureInPictureWindowManager::GetInstance()->GetOcclusionTracker()) {
    tracker->OnPictureInPictureWidgetOpened(mini_player.get());
  }
  return mini_player;
}

MiniPlayer::MiniPlayer(
    content::VideoPictureInPictureWindowController* controller)
    : controller_(controller) {}

MiniPlayer::~MiniPlayer() {
  placement_.reset();
  if (dock_) {
    // The video goes with this mini player, not with the window it docked in.
    dock_->Release();
    dock_ = nullptr;
    view_ = nullptr;
  }
  PictureInPictureWindowManager::GetInstance()->OnPictureInPictureWindowHidden(
      this);
}

bool MiniPlayer::IsActive() const {
  return views::Widget::IsActive();
}

void MiniPlayer::Close() {
  shown_ = false;
  Undock();
  view_->DetachFromCompositor();
  views::Widget::Close();
  if (fade_animator_) {
    fade_animator_->CancelAndReset();
  }
  PictureInPictureWindowManager::GetInstance()->OnPictureInPictureWindowHidden(
      this);
}

void MiniPlayer::ShowInactive() {
  shown_ = true;
  MiniPlayerStack::Get().OnShown(controller_->GetWebContents());
  view_->SetMuted(controller_->GetMuteStatus());
  view_->ShowControls(false);
  placement_->Update();
  PictureInPictureWindowManager::GetInstance()->OnPictureInPictureWindowShown(
      this);
}

void MiniPlayer::Hide() {
  if (shown_ && controller_->GetWebContents()) {
    MiniPlayerStack::Get().OnHidden(controller_->GetWebContents(),
                                    view_->still(), view_->position());
  }
  shown_ = false;
  Undock();
  views::Widget::Hide();
  if (fade_animator_) {
    fade_animator_->CancelAndReset();
  }
  view_->DetachFromCompositor();
  PictureInPictureWindowManager::GetInstance()->OnPictureInPictureWindowHidden(
      this);
}

bool MiniPlayer::IsVisible() const {
  return shown_;
}

gfx::Rect MiniPlayer::GetBounds() {
  if (dock_) {
    return view_->GetBoundsInScreen();
  }
  return native_widget() ? GetWindowBoundsInScreen() : gfx::Rect();
}

void MiniPlayer::UpdateNaturalSize(const gfx::Size& natural_size) {
  view_->SetNaturalSize(natural_size);
  SetAspectRatio(gfx::SizeF(natural_size));
  SetBounds(CalculateBounds());
  if (dock_) {
    dock_->UpdateShape();
  }
}

void MiniPlayer::SetPlaybackState(PlaybackState playback_state) {
  view_->SetPlaybackState(playback_state);
}

void MiniPlayer::SetPlayPauseButtonVisibility(bool is_visible) {
  play_pause_shown_ = is_visible;
  view_->SetSiteButtonVisible(MiniPlayerView::SiteButton::kPlayPause,
                              is_visible);
}

void MiniPlayer::SetSkipAdButtonVisibility(bool is_visible) {
  view_->SetSiteButtonVisible(MiniPlayerView::SiteButton::kSkipAd, is_visible);
}

void MiniPlayer::SetNextTrackButtonVisibility(bool is_visible) {
  view_->SetSiteButtonVisible(MiniPlayerView::SiteButton::kNextTrack,
                              is_visible);
}

void MiniPlayer::SetPreviousTrackButtonVisibility(bool is_visible) {
  view_->SetSiteButtonVisible(MiniPlayerView::SiteButton::kPreviousTrack,
                              is_visible);
}

void MiniPlayer::SetMicrophoneMuted(bool muted) {
  view_->SetMicrophoneMuted(muted);
}

void MiniPlayer::SetCameraState(bool turned_on) {
  view_->SetCameraOn(turned_on);
}

void MiniPlayer::SetMediaMuted(bool muted) {
  view_->SetMuted(muted);
}

void MiniPlayer::SetToggleMicrophoneButtonVisibility(bool is_visible) {
  view_->SetSiteButtonVisible(MiniPlayerView::SiteButton::kMicrophone,
                              is_visible);
}

void MiniPlayer::SetToggleCameraButtonVisibility(bool is_visible) {
  view_->SetSiteButtonVisible(MiniPlayerView::SiteButton::kCamera, is_visible);
}

void MiniPlayer::SetHangUpButtonVisibility(bool is_visible) {
  view_->SetSiteButtonVisible(MiniPlayerView::SiteButton::kHangUp, is_visible);
}

void MiniPlayer::SetNextSlideButtonVisibility(bool is_visible) {
  view_->SetSiteButtonVisible(MiniPlayerView::SiteButton::kNextSlide,
                              is_visible);
}

void MiniPlayer::SetPreviousSlideButtonVisibility(bool is_visible) {
  view_->SetSiteButtonVisible(MiniPlayerView::SiteButton::kPreviousSlide,
                              is_visible);
}

void MiniPlayer::SetMediaPosition(
    const media_session::MediaPosition& position) {
  view_->SetMediaPosition(position);
}

void MiniPlayer::SetSurfaceId(const viz::SurfaceId& surface_id) {
  view_->SetSurfaceId(surface_id);
}

void MiniPlayer::SetPlaybackControlsVisibility(bool is_visible) {
  view_->SetControlsAllowed(is_visible);
}

gfx::Size MiniPlayer::GetMinimumSize() const {
  return kMinimumSize;
}

gfx::Size MiniPlayer::GetMaximumSize() const {
  return max_size_;
}

void MiniPlayer::OnNativeWidgetMove() {
  views::Widget::OnNativeWidgetMove();
  if (has_been_shown_ && views::Widget::IsVisible()) {
    LastBounds() = GetWindowBoundsInScreen();
  }
}

void MiniPlayer::OnNativeWidgetSizeChanged(const gfx::Size& new_size) {
  views::Widget::OnNativeWidgetSizeChanged(new_size);
  if (has_been_shown_ && views::Widget::IsVisible()) {
    LastBounds() = GetWindowBoundsInScreen();
  }
  controller_->UpdateLayerBounds();
}

void MiniPlayer::OnNativeWidgetDestroying() {
  view_->DetachFromCompositor();
  views::Widget::OnNativeWidgetDestroying();
}

void MiniPlayer::OnNativeWidgetDestroyed() {
  views::Widget::OnNativeWidgetDestroyed();
  // This deletes `this`.
  controller_->OnWindowDestroyed(/*should_pause_video=*/play_pause_shown_);
}

void MiniPlayer::SetForcedTucking(bool tuck) {
  SetOpacity(tuck ? 0 : 1);
}

#if BUILDFLAG(IS_MAC)
void MiniPlayer::OnAnyBrowserEnteredFullscreen() {
  MoveToActiveFullscreenSpace();
}
#endif

void MiniPlayer::Place(MiniPlayerDock* dock) {
  if (!shown_) {
    return;
  }
  if (!dock) {
    Undock();
    ShowFloating();
    return;
  }
  if (dock == dock_) {
    return;
  }
  Undock();
  if (fade_animator_) {
    fade_animator_->CancelAndReset();
  }
  views::Widget::Hide();
  view_->set_moves_widget(false);
  dock_ = dock;
  view_ = dock->Take(client_->RemoveChildViewT(view_.get()));
  dock->set_on_destroying(base::BindOnce(
      [](MiniPlayer* mini_player) {
        mini_player->Undock();
        mini_player->placement_->ScheduleUpdate();
      },
      base::Unretained(this)));
  placement_->set_current(dock_);
  controller_->UpdateLayerBounds();
}

void MiniPlayer::Undock() {
  if (!dock_) {
    return;
  }
  view_ = client_->AddChildView(dock_->Release());
  dock_ = nullptr;
  view_->SetFloatingLook();
  view_->set_moves_widget(true);
  placement_->set_current(nullptr);
}

void MiniPlayer::ShowFloating() {
  if (views::Widget::IsVisible()) {
    return;
  }
  SetBounds(CalculateBounds());
  if (!fade_animator_) {
    fade_animator_ = std::make_unique<PictureInPictureWidgetFadeAnimator>();
  }
  fade_animator_->AnimateShowWindow(
      this, PictureInPictureWidgetFadeAnimator::WidgetShowType::kShowInactive);
  views::Widget::SetVisibleOnAllWorkspaces(true);
  has_been_shown_ = true;
  controller_->UpdateLayerBounds();
}

gfx::Rect MiniPlayer::GetWorkArea() const {
  return display::Screen::Get()
      ->GetDisplayNearestWindow(
          native_widget() && views::Widget::IsVisible()
              ? GetNativeWindow()
              : controller_->GetWebContents()->GetTopLevelNativeWindow())
      .work_area();
}

gfx::Rect MiniPlayer::CalculateBounds() {
  const gfx::Rect work_area = GetWorkArea();
  max_size_ = gfx::Size(work_area.width() * 0.8, work_area.height() * 0.8);
  max_size_.SetToMax(kMinimumSize);

  std::optional<gfx::Rect> last =
      has_been_shown_ ? GetWindowBoundsInScreen() : LastBounds();
  gfx::Size size = last ? last->size() : gfx::Size(work_area.width() / 5, 0);
  const gfx::Size natural = view_->natural_size();
  if (!natural.IsEmpty()) {
    // Keep the width, and give the video its shape.
    size.set_height(size.width() * natural.height() / natural.width());
    const double shrink =
        std::min({1.0, static_cast<double>(max_size_.width()) / size.width(),
                  static_cast<double>(max_size_.height()) / size.height()});
    const double grow =
        std::max({1.0, static_cast<double>(kMinimumSize.width()) / size.width(),
                  static_cast<double>(kMinimumSize.height()) / size.height()});
    const double scale = shrink < 1 ? shrink : grow;
    size = gfx::Size(size.width() * scale, size.height() * scale);
  } else if (size.height() == 0) {
    size.set_height(size.width() * 9 / 16);
  }

  // A new mini player starts in the bottom right corner. One that was moved
  // keeps the corner it is nearest, so a new shape grows away from it.
  const int margin = (work_area.width() + work_area.height()) / 2 / 50;
  gfx::Rect bounds(work_area.right() - margin - size.width(),
                   work_area.bottom() - margin - size.height(), size.width(),
                   size.height());
  if (last) {
    const gfx::Point center = last->CenterPoint();
    bounds.set_x(center.x() < work_area.CenterPoint().x()
                     ? last->x()
                     : last->right() - size.width());
    bounds.set_y(center.y() < work_area.CenterPoint().y()
                     ? last->y()
                     : last->bottom() - size.height());
  }
  bounds.AdjustToFit(work_area);
  return bounds;
}

}  // namespace uw
