// Copyright 2026 The uw Authors. All rights reserved.

#ifndef UW_BROWSER_UI_WEBUI_UW_CHAT_UW_NEW_TAB_HANDLER_H_
#define UW_BROWSER_UI_WEBUI_UW_CHAT_UW_NEW_TAB_HANDLER_H_

#include <map>
#include <optional>
#include <string>

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/scoped_observation.h"
#include "base/task/cancelable_task_tracker.h"
#include "base/time/time.h"
#include "components/prefs/pref_change_registrar.h"
#include "mojo/public/cpp/bindings/pending_receiver.h"
#include "mojo/public/cpp/bindings/pending_remote.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "url/gurl.h"
#include "uw/browser/agent/chat_alerts.h"
#include "uw/browser/agent/chat_index.h"
#include "uw/browser/agent/chat_tab.h"
#include "uw/browser/agent/scheduled_chats.h"
#include "uw/browser/ui/webui/uw_chat/uw_chat.mojom.h"

class Profile;

namespace content {
class WebUI;
}  // namespace content

namespace uw {

// The new tab page's browser side: what its box asks, and the state of uw's
// features around it. See docs/plans/new-tab-page.md.
class UwNewTabHandler : public mojom::NewTabHandler,
                        public ChatAlerts::Observer,
                        public ChatIndex::Observer,
                        public ScheduledChats::Observer {
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

  // ChatAlerts::Observer:
  void OnAlertsChanged() override;
  // ChatIndex::Observer:
  void OnChatsChanged() override;
  // ScheduledChats::Observer:
  void OnScheduleChanged(const std::string& chat_id) override;

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
  base::ScopedObservation<ChatAlerts, ChatAlerts::Observer> alerts_{this};
  base::ScopedObservation<ChatIndex, ChatIndex::Observer> index_{this};
  base::ScopedObservation<ScheduledChats, ScheduledChats::Observer> schedules_{
      this};
  base::WeakPtrFactory<UwNewTabHandler> weak_factory_{this};
};

}  // namespace uw

#endif  // UW_BROWSER_UI_WEBUI_UW_CHAT_UW_NEW_TAB_HANDLER_H_
