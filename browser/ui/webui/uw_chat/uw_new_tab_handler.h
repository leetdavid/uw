// Copyright 2026 The uw Authors. All rights reserved.

#ifndef UW_BROWSER_UI_WEBUI_UW_CHAT_UW_NEW_TAB_HANDLER_H_
#define UW_BROWSER_UI_WEBUI_UW_CHAT_UW_NEW_TAB_HANDLER_H_

#include <map>
#include <optional>
#include <string>

#include "base/callback_list.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/scoped_observation.h"
#include "base/task/cancelable_task_tracker.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "chrome/browser/picture_in_picture/picture_in_picture_window_manager.h"
#include "components/prefs/pref_change_registrar.h"
#include "mojo/public/cpp/bindings/pending_receiver.h"
#include "mojo/public/cpp/bindings/pending_remote.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "services/media_session/public/cpp/media_position.h"
#include "services/media_session/public/mojom/media_session.mojom.h"
#include "url/gurl.h"
#include "uw/browser/agent/chat_alerts.h"
#include "uw/browser/agent/chat_index.h"
#include "uw/browser/agent/chat_tab.h"
#include "uw/browser/agent/scheduled_chats.h"
#include "uw/browser/ui/webui/uw_chat/uw_chat.mojom.h"

class BrowserWindowInterface;
class Profile;

namespace content {
class WebContents;
class WebUI;
}  // namespace content

namespace uw {

// The new tab page's browser side: what its box asks, and the state of uw's
// features around it. See docs/plans/new-tab-page.md.
class UwNewTabHandler : public mojom::NewTabHandler,
                        public ChatAlerts::Observer,
                        public ChatIndex::Observer,
                        public ScheduledChats::Observer,
                        public PictureInPictureWindowManager::Observer,
                        public media_session::mojom::MediaSessionObserver {
 public:
  UwNewTabHandler(content::WebUI* web_ui,
                  mojo::PendingRemote<mojom::NewTabPage> page,
                  mojo::PendingReceiver<mojom::NewTabHandler> receiver);
  UwNewTabHandler(const UwNewTabHandler&) = delete;
  UwNewTabHandler& operator=(const UwNewTabHandler&) = delete;
  ~UwNewTabHandler() override;

  // mojom::NewTabHandler:
  void NewSchedule(NewScheduleCallback callback) override;
  void Ask(const std::string& text,
           const std::string& provider_id,
           const std::string& model) override;
  void AskOnSchedule(mojom::ChatSchedulePtr schedule) override;
  void OpenChat(const std::string& chat_id,
                const std::string& alert_id) override;
  void OpenPage(const std::string& chat_id,
                const std::string& alert_id) override;
  void Dismiss(const std::string& chat_id,
               const std::string& alert_id) override;
  void ShowInbox() override;
  void ShowScheduled() override;
  void ShowTab(int32_t tab) override;
  void PlayOrPause(int32_t tab) override;
  void UndoChange() override;
  void Pause(mojom::OrganizationPause pause) override;

  // ChatAlerts::Observer:
  void OnAlertsChanged() override;
  // ChatIndex::Observer:
  void OnChatsChanged() override;
  // ScheduledChats::Observer:
  void OnScheduleChanged(const std::string& chat_id) override;
  // PictureInPictureWindowManager::Observer:
  void OnEnterPictureInPicture() override;
  void OnExitPictureInPicture() override;
  // media_session::mojom::MediaSessionObserver:
  void MediaSessionInfoChanged(
      media_session::mojom::MediaSessionInfoPtr info) override;
  void MediaSessionMetadataChanged(
      const std::optional<media_session::MediaMetadata>& metadata) override;
  void MediaSessionActionsChanged(
      const std::vector<media_session::mojom::MediaSessionAction>& actions)
      override {}
  void MediaSessionImagesChanged(
      const base::flat_map<media_session::mojom::MediaSessionImageType,
                           std::vector<media_session::MediaImage>>& images)
      override {}
  void MediaSessionPositionChanged(
      const std::optional<media_session::MediaPosition>& position) override;

  // How a chat that finished out of sight ended, read from its file.
  struct Ending {
    // The chat's last message from the user when it was read. It is read
    // again once that changes.
    base::Time updated;
    // "Answered" or "Didn't finish", and the agent's last words.
    std::string what;
    std::string text;
    // When the chat was last saved, or null until it is read.
    base::Time time;
  };

 private:
  Profile* profile() const;
  void SendBox();
  void SendSections();
  // A row of the chat `entry`, with its watched page's title and icon.
  mojom::NewTabRowPtr RowOf(const ChatIndex::Entry& entry);
  void OnLooked(const std::string& page,
                const std::string& title,
                const std::string& favicon);
  void OnRead(const std::string& chat_id, Ending ending);
  // The row leaves Finished: its alert is seen, or its chat read.
  void Remove(const std::string& chat_id, const std::string& alert_id);
  // Asks each terminal tab whether a program runs in it, while the page
  // shows. Shells tell no one when a command starts or ends.
  void Poll();
  void OnPolled(std::vector<std::pair<int32_t, std::string>> programs);
  // Follows the media session of the mini player's video, if it is this
  // profile's.
  void WatchVideo();
  // The window the page is in, or null.
  BrowserWindowInterface* Window() const;
  mojom::NewTabTabsPtr TabsOf(BrowserWindowInterface* window) const;
  // Shows `url` in this tab, as a chat that starts with `start` when given.
  void Become(const GURL& url, std::optional<ChatStart> start);

  const raw_ptr<content::WebUI> web_ui_;
  mojo::Remote<mojom::NewTabPage> page_;
  mojo::Receiver<mojom::NewTabHandler> receiver_;
  // Ask works once a provider serves the agent model.
  PrefChangeRegistrar prefs_;
  // Watched pages' titles and icons, empty until they are looked up.
  std::map<std::string, std::pair<std::string, std::string>> pages_;
  std::map<std::string, Ending> endings_;
  base::CancelableTaskTracker tracker_;
  base::RepeatingTimer poll_;
  // The program each busy terminal tab runs, by the tab's handle.
  std::map<int32_t, std::string> busy_;
  base::WeakPtr<content::WebContents> video_;
  bool playing_ = false;
  std::u16string video_title_;
  std::optional<media_session::MediaPosition> position_;
  mojo::Receiver<media_session::mojom::MediaSessionObserver> video_session_{
      this};
  base::ScopedObservation<PictureInPictureWindowManager,
                          PictureInPictureWindowManager::Observer>
      pictures_{this};
  // Automatic organization's moves, and the window's tree, which Undo changes.
  base::CallbackListSubscription organizer_events_;
  base::CallbackListSubscription tree_changes_;
  base::ScopedObservation<ChatAlerts, ChatAlerts::Observer> alerts_{this};
  base::ScopedObservation<ChatIndex, ChatIndex::Observer> index_{this};
  base::ScopedObservation<ScheduledChats, ScheduledChats::Observer> schedules_{
      this};
  base::WeakPtrFactory<UwNewTabHandler> weak_factory_{this};
};

}  // namespace uw

#endif  // UW_BROWSER_UI_WEBUI_UW_CHAT_UW_NEW_TAB_HANDLER_H_
