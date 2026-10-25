// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/browser/ui/views/mini_player/playback_controls.h"

#include <algorithm>
#include <utility>

#include "base/functional/bind.h"
#include "base/strings/utf_string_conversions.h"
#include "cc/paint/paint_flags.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/color/chrome_color_id.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/grit/generated_resources.h"
#include "components/vector_icons/vector_icons.h"
#include "content/public/browser/favicon_status.h"
#include "content/public/browser/media_session.h"
#include "content/public/browser/media_session_service.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/browser/picture_in_picture_window_controller.h"
#include "content/public/browser/web_contents.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "third_party/skia/include/core/SkPath.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/color/color_provider.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/color_utils.h"
#include "ui/gfx/font_list.h"
#include "ui/gfx/geometry/skia_conversions.h"
#include "ui/gfx/image/image_skia_operations.h"
#include "ui/gfx/scoped_canvas.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/controls/button/image_button.h"
#include "ui/views/controls/label.h"
#include "ui/views/view_class_properties.h"
#include "uw/browser/ui/vector_icons/vector_icons.h"
#include "uw/browser/ui/views/mini_player/mini_player_stack.h"

namespace uw {
namespace {

using media_session::mojom::MediaPlaybackState;
using media_session::mojom::MediaSessionAction;
using media_session::mojom::MediaSessionImageType;

// The gap to whatever is above: the mini player, or the tab list.
constexpr int kTopGap = 6;
constexpr int kPadding = 6;
constexpr int kArt = 32;
constexpr int kArtRadius = 6;
constexpr int kRadius = 10;
constexpr int kGap = 9;
constexpr int kButton = 24;
constexpr int kIcon = 13;

gfx::FontList FontOfSize(int size, gfx::Font::Weight weight) {
  const gfx::FontList& base = views::Label::GetDefaultFontList();
  return base.DeriveWithSizeDelta(size - base.GetFontSize())
      .DeriveWithWeight(weight);
}

}  // namespace

PlaybackControls::PlaybackControls(BrowserWindowInterface* window)
    : views::Button(base::BindRepeating(&PlaybackControls::GoToTab,
                                        base::Unretained(this))),
      window_(window) {
  SetVisible(false);
  SetProperty(views::kMarginsKey, gfx::Insets::TLBR(kTopGap, 0, 0, 0));
  SetTooltipText(l10n_util::GetStringUTF16(IDS_UW_PLAYBACK_CONTROLS_GO_TO_TAB));
  auto button = [this](views::Button::PressedCallback callback, int message) {
    auto* added =
        AddChildView(std::make_unique<views::ImageButton>(std::move(callback)));
    const std::u16string name = l10n_util::GetStringUTF16(message);
    added->SetTooltipText(name);
    added->GetViewAccessibility().SetName(name);
    added->SetImageHorizontalAlignment(views::ImageButton::ALIGN_CENTER);
    added->SetImageVerticalAlignment(views::ImageButton::ALIGN_MIDDLE);
    return added;
  };
  previous_ = button(
      base::BindRepeating(
          [](PlaybackControls* self) {
            if (self->tab_) {
              content::MediaSession::Get(self->tab_.get())->PreviousTrack();
            }
          },
          base::Unretained(this)),
      IDS_PICTURE_IN_PICTURE_PREVIOUS_TRACK_CONTROL_ACCESSIBLE_TEXT);
  play_pause_ = button(base::BindRepeating(&PlaybackControls::PlayOrPause,
                                           base::Unretained(this)),
                       IDS_PICTURE_IN_PICTURE_PAUSE_CONTROL_TEXT);
  next_ =
      button(base::BindRepeating(
                 [](PlaybackControls* self) {
                   if (self->tab_) {
                     content::MediaSession::Get(self->tab_.get())->NextTrack();
                   }
                 },
                 base::Unretained(this)),
             IDS_PICTURE_IN_PICTURE_NEXT_TRACK_CONTROL_ACCESSIBLE_TEXT);
  UpdateButtons();

  window_->GetTabStripModel()->AddObserver(this);
  picture_in_picture_.Observe(PictureInPictureWindowManager::GetInstance());
  stack_ = MiniPlayerStack::Get().Observe(base::BindRepeating(
      &PlaybackControls::UpdateVisibility, base::Unretained(this)));
  mojo::Remote<media_session::mojom::AudioFocusManager> focus;
  content::GetMediaSessionService().BindAudioFocusManager(
      focus.BindNewPipeAndPassReceiver());
  focus->AddObserver(focus_.BindNewPipeAndPassRemote());
}

PlaybackControls::~PlaybackControls() = default;

gfx::Size PlaybackControls::CalculatePreferredSize(
    const views::SizeBounds& available_size) const {
  return gfx::Size(available_size.width().is_bounded()
                       ? available_size.width().value()
                       : width(),
                   kHeight);
}

void PlaybackControls::Layout(PassKey) {
  int x = width() - kPadding;
  for (views::ImageButton* button : {next_, play_pause_, previous_}) {
    if (button->GetVisible()) {
      x -= kButton;
      button->SetBounds(x, (height() - kButton) / 2, kButton, kButton);
    }
  }
}

void PlaybackControls::OnThemeChanged() {
  views::Button::OnThemeChanged();
  UpdateButtons();
}

void PlaybackControls::StateChanged(ButtonState old_state) {
  views::Button::StateChanged(old_state);
  UpdateButtons();
}

void PlaybackControls::OnTabStripModelChanged(
    TabStripModel* tab_strip_model,
    const TabStripModelChange& change,
    const TabStripSelectionChange& selection) {
  if (change.type() == TabStripModelChange::kRemoved && tab_) {
    for (const TabStripModelChange::RemovedTab& removed :
         change.GetRemove()->contents) {
      if (removed.contents == tab_.get()) {
        tab_.reset();
        session_.reset();
      }
    }
  }
  UpdateVisibility();
}

void PlaybackControls::OnEnterPictureInPicture() {
  UpdateVisibility();
}

void PlaybackControls::OnExitPictureInPicture() {
  UpdateVisibility();
}

void PlaybackControls::OnFocusGained(
    media_session::mojom::AudioFocusRequestStatePtr session) {
  if (!session->request_id) {
    return;
  }
  content::WebContents* contents =
      content::MediaSession::GetWebContentsFromRequestId(*session->request_id);
  if (contents && window_->GetTabStripModel()->GetIndexOfWebContents(
                      contents) != TabStripModel::kNoTab) {
    Follow(contents);
  }
}

void PlaybackControls::MediaSessionInfoChanged(
    media_session::mojom::MediaSessionInfoPtr session_info) {
  playing_ = session_info &&
             session_info->playback_state == MediaPlaybackState::kPlaying;
  UpdateButtons();
  SchedulePaint();
}

void PlaybackControls::MediaSessionMetadataChanged(
    const std::optional<media_session::MediaMetadata>& metadata) {
  if (!tab_) {
    return;
  }
  title_ =
      metadata && !metadata->title.empty() ? metadata->title : tab_->GetTitle();
  source_ = metadata && !metadata->artist.empty() ? metadata->artist
            : metadata && !metadata->source_title.empty()
                ? metadata->source_title
                : base::UTF8ToUTF16(tab_->GetLastCommittedURL().host());
  GetViewAccessibility().SetName(title_);
  SchedulePaint();
}

void PlaybackControls::MediaSessionActionsChanged(
    const std::vector<MediaSessionAction>& actions) {
  has_previous_ =
      std::ranges::contains(actions, MediaSessionAction::kPreviousTrack);
  has_next_ = std::ranges::contains(actions, MediaSessionAction::kNextTrack);
  UpdateButtons();
}

void PlaybackControls::MediaSessionImagesChanged(
    const base::flat_map<MediaSessionImageType,
                         std::vector<media_session::MediaImage>>& images) {
  art_ = gfx::ImageSkia();
  SchedulePaint();
  const auto artwork = images.find(MediaSessionImageType::kArtwork);
  if (!tab_ || artwork == images.end() || artwork->second.empty()) {
    return;
  }
  content::MediaSession::Get(tab_.get())
      ->GetMediaImageBitmap(
          artwork->second.front(), kArt, kArt * 2,
          base::BindOnce(&PlaybackControls::OnArt, weak_factory_.GetWeakPtr()));
}

void PlaybackControls::Follow(content::WebContents* contents) {
  if (contents == tab_.get()) {
    return;
  }
  tab_ = contents->GetWeakPtr();
  playing_ = false;
  has_previous_ = false;
  has_next_ = false;
  art_ = gfx::ImageSkia();
  MediaSessionMetadataChanged(std::nullopt);
  session_.reset();
  content::MediaSession::Get(contents)->AddObserver(
      session_.BindNewPipeAndPassRemote());
  UpdateButtons();
  UpdateVisibility();
}

void PlaybackControls::UpdateVisibility() {
  content::WebContents* contents = tab_.get();
  PictureInPictureWindowManager* manager =
      PictureInPictureWindowManager::GetInstance();
  content::PictureInPictureWindowController* kept =
      manager->kept_window_controller();
  // A tab whose site's window is kept beside a video has its window's own
  // controls too.
  const bool in_mini_player =
      contents &&
      (manager->GetWebContents() == contents ||
       (kept && kept->GetWebContents() == contents) ||
       std::ranges::any_of(MiniPlayerStack::Get().paused(),
                           [contents](const MiniPlayerStack::Entry& entry) {
                             return entry.contents.get() == contents;
                           }));
  const bool show =
      contents && !in_mini_player &&
      window_->GetTabStripModel()->GetActiveWebContents() != contents;
  if (show != GetVisible()) {
    SetVisible(show);
    PreferredSizeChanged();
  }
}

void PlaybackControls::UpdateButtons() {
  const bool hovered =
      GetState() == STATE_HOVERED || GetState() == STATE_PRESSED;
  previous_->SetVisible(hovered && has_previous_);
  next_->SetVisible(hovered && has_next_);
  const std::u16string name = l10n_util::GetStringUTF16(
      playing_ ? IDS_PICTURE_IN_PICTURE_PAUSE_CONTROL_TEXT
               : IDS_PICTURE_IN_PICTURE_PLAY_CONTROL_TEXT);
  play_pause_->SetTooltipText(name);
  play_pause_->GetViewAccessibility().SetName(name);
  if (GetColorProvider()) {
    const SkColor color = TextColor();
    previous_->SetImageModel(
        views::Button::STATE_NORMAL,
        ui::ImageModel::FromVectorIcon(vector_icons::kSkipPreviousIcon, color,
                                       kIcon));
    play_pause_->SetImageModel(
        views::Button::STATE_NORMAL,
        ui::ImageModel::FromVectorIcon(
            playing_ ? kMiniPlayerPauseIcon : kMiniPlayerPlayIcon, color,
            kIcon));
    next_->SetImageModel(views::Button::STATE_NORMAL,
                         ui::ImageModel::FromVectorIcon(
                             vector_icons::kSkipNextIcon, color, kIcon));
  }
  InvalidateLayout();
  SchedulePaint();
}

void PlaybackControls::OnArt(const SkBitmap& art) {
  art_ =
      art.isNull() ? gfx::ImageSkia() : gfx::ImageSkia::CreateFrom1xBitmap(art);
  SchedulePaint();
}

void PlaybackControls::GoToTab() {
  TabStripModel* tabs = window_->GetTabStripModel();
  const int index =
      tab_ ? tabs->GetIndexOfWebContents(tab_.get()) : TabStripModel::kNoTab;
  if (index != TabStripModel::kNoTab) {
    tabs->ActivateTabAt(index);
  }
}

void PlaybackControls::PlayOrPause() {
  if (!tab_) {
    return;
  }
  content::MediaSession* session = content::MediaSession::Get(tab_.get());
  if (playing_) {
    session->Suspend(content::MediaSession::SuspendType::kUI);
  } else {
    session->Resume(content::MediaSession::SuspendType::kUI);
  }
}

SkColor PlaybackControls::TextColor() const {
  return GetColorProvider() ? GetColorProvider()->GetColor(
                                  kColorTabForegroundInactiveFrameActive)
                            : SK_ColorBLACK;
}

void PlaybackControls::PaintButtonContents(gfx::Canvas* canvas) {
  const SkColor text = TextColor();
  const bool light = color_utils::IsDark(text);
  const bool hovered =
      GetState() == STATE_HOVERED || GetState() == STATE_PRESSED;
  cc::PaintFlags fill;
  fill.setAntiAlias(true);
  fill.setColor(SkColorSetA(
      SK_ColorWHITE, hovered ? (light ? 0xFF : 0x29) : (light ? 0x75 : 0x14)));
  canvas->DrawRoundRect(gfx::RectF(GetLocalBounds()), kRadius, fill);

  // A soft circle behind the button under the pointer.
  for (views::ImageButton* button : {previous_, play_pause_, next_}) {
    if (button->GetVisible() && button->IsMouseHovered()) {
      fill.setColor(SkColorSetA(text, 0x14));
      canvas->DrawCircle(gfx::PointF(button->bounds().CenterPoint()),
                         kButton / 2.0f, fill);
    }
  }

  // The art, or the site's icon on a faint tile.
  gfx::ImageSkia favicon;
  if (tab_) {
    if (content::NavigationEntry* page =
            tab_->GetController().GetLastCommittedEntry()) {
      favicon = page->GetFavicon().image.AsImageSkia();
    }
  }
  const gfx::Rect art_box(kPadding, (height() - kArt) / 2, kArt, kArt);
  {
    gfx::ScopedCanvas scoped(canvas);
    canvas->ClipPath(
        SkPath::RRect(gfx::RectToSkRect(art_box), kArtRadius, kArtRadius),
        true);
    if (!art_.isNull()) {
      const gfx::Size image = art_.size();
      const float scale = std::max(static_cast<float>(kArt) / image.width(),
                                   static_cast<float>(kArt) / image.height());
      const gfx::Size source(kArt / scale, kArt / scale);
      canvas->DrawImageInt(art_, (image.width() - source.width()) / 2,
                           (image.height() - source.height()) / 2,
                           source.width(), source.height(), art_box.x(),
                           art_box.y(), kArt, kArt, true);
    } else {
      canvas->FillRect(art_box, SkColorSetA(text, 0x14));
      if (!favicon.isNull()) {
        canvas->DrawImageInt(
            gfx::ImageSkiaOperations::CreateResizedImage(
                favicon, skia::ImageOperations::RESIZE_BEST, gfx::Size(16, 16)),
            art_box.CenterPoint().x() - 8, art_box.CenterPoint().y() - 8);
      }
    }
  }

  // The title, then the site's icon and where the sound comes from, or that
  // it is paused.
  int right = width() - kPadding;
  for (views::ImageButton* button : {previous_, play_pause_, next_}) {
    if (button->GetVisible()) {
      right = std::min(right, button->x());
    }
  }
  right -= 4;
  const int left = art_box.right() + kGap;
  canvas->DrawStringRectWithFlags(
      title_, FontOfSize(13, gfx::Font::Weight::MEDIUM), text,
      gfx::Rect(left, 6, right - left, 17), gfx::Canvas::NO_SUBPIXEL_RENDERING);
  int x = left;
  const int line = 25;
  if (!favicon.isNull()) {
    canvas->DrawImageInt(
        gfx::ImageSkiaOperations::CreateResizedImage(
            favicon, skia::ImageOperations::RESIZE_BEST, gfx::Size(12, 12)),
        x, line);
    x += 12 + 5;
  }
  canvas->DrawStringRectWithFlags(
      playing_ ? source_
               : l10n_util::GetStringUTF16(IDS_UW_PLAYBACK_CONTROLS_PAUSED),
      FontOfSize(11, gfx::Font::Weight::NORMAL), SkColorSetA(text, 0x94),
      gfx::Rect(x, line - 1, right - x, 14),
      gfx::Canvas::NO_SUBPIXEL_RENDERING);
}

BEGIN_METADATA(PlaybackControls)
END_METADATA

}  // namespace uw
