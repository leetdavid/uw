// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/browser/ui/views/mini_player/auto_mini_player.h"

#include <algorithm>

#include "base/functional/bind.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/picture_in_picture/picture_in_picture_window_manager.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/media_session.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_user_data.h"
#include "services/media_session/public/mojom/media_session.mojom.h"
#include "uw/browser/ui/views/mini_player/mini_player_prefs.h"
#include "uw/browser/ui/views/mini_player/mini_player_stack.h"

namespace uw {
namespace {

using media_session::mojom::MediaAudioVideoState;
using media_session::mojom::MediaPlaybackState;

// Marks a tab whose mini player uw opened on a tab switch.
class OpenedOnSwitch : public content::WebContentsUserData<OpenedOnSwitch> {
 public:
  ~OpenedOnSwitch() override = default;

 private:
  friend class content::WebContentsUserData<OpenedOnSwitch>;
  explicit OpenedOnSwitch(content::WebContents* contents)
      : content::WebContentsUserData<OpenedOnSwitch>(*contents) {}
  WEB_CONTENTS_USER_DATA_KEY_DECL();
};

WEB_CONTENTS_USER_DATA_KEY_IMPL(OpenedOnSwitch);

// A video that plays with its sound on, neither it nor its tab muted.
bool PlaysAudibleVideo(content::WebContents* contents) {
  if (contents->IsAudioMuted()) {
    return false;
  }
  const media_session::mojom::MediaSessionInfoPtr info =
      content::MediaSession::Get(contents)->GetMediaSessionInfoSync();
  return info && !info->muted &&
         info->playback_state == MediaPlaybackState::kPlaying &&
         info->audio_video_states &&
         std::ranges::contains(*info->audio_video_states,
                               MediaAudioVideoState::kAudioVideo);
}

}  // namespace

AutoMiniPlayer::AutoMiniPlayer(BrowserWindowInterface* window)
    : window_(window) {
  window_->GetTabStripModel()->AddObserver(this);
}

AutoMiniPlayer::~AutoMiniPlayer() = default;

// static
bool AutoMiniPlayer::OpenedOnTabSwitch(content::WebContents* contents) {
  return contents && OpenedOnSwitch::FromWebContents(contents);
}

void AutoMiniPlayer::OnTabStripModelChanged(
    TabStripModel* tab_strip_model,
    const TabStripModelChange& change,
    const TabStripSelectionChange& selection) {
  if (!selection.active_tab_changed()) {
    return;
  }
  if (selection.new_contents) {
    MiniPlayerStack::Get().Remove(selection.new_contents);
    PutBack(selection.new_contents);
  }
  if (selection.old_contents && !tab_strip_model->empty() &&
      tab_strip_model->GetIndexOfWebContents(selection.old_contents) !=
          TabStripModel::kNoTab) {
    MaybeOpen(selection.old_contents);
  }
}

void AutoMiniPlayer::MaybeOpen(content::WebContents* contents) {
  PrefService* local_state = g_browser_process->local_state();
  if (MiniPlayerModeOf(local_state) != MiniPlayerMode::kDock ||
      !local_state->GetBoolean(kMiniPlayerOnTabSwitchPref) ||
      PictureInPictureWindowManager::GetInstance()->GetWebContents() ==
          contents ||
      !PlaysAudibleVideo(contents)) {
    return;
  }
  // A small video, such as a muted preview or an ad beside the page, stays.
  content::MediaSession::Get(contents)->GetVisibility(base::BindOnce(
      [](base::WeakPtr<AutoMiniPlayer> self,
         base::WeakPtr<content::WebContents> contents, bool visible) {
        if (!self || !contents || !visible ||
            self->window_->GetTabStripModel()->GetActiveWebContents() ==
                contents.get()) {
          return;
        }
        OpenedOnSwitch::CreateForWebContents(contents.get());
        content::MediaSession::Get(contents.get())->EnterPictureInPicture();
      },
      weak_factory_.GetWeakPtr(), contents->GetWeakPtr()));
}

void AutoMiniPlayer::PutBack(content::WebContents* contents) {
  if (!OpenedOnSwitch::FromWebContents(contents)) {
    return;
  }
  contents->RemoveUserData(OpenedOnSwitch::UserDataKey());
  if (PictureInPictureWindowManager::GetInstance()->GetWebContents() ==
      contents) {
    PictureInPictureWindowManager::GetInstance()->ExitPictureInPicture();
  }
}

}  // namespace uw
