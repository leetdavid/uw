// Copyright 2026 The uw Authors. All rights reserved.

#ifndef UW_BROWSER_UI_WEBUI_UW_CHAT_UW_INBOX_HANDLER_H_
#define UW_BROWSER_UI_WEBUI_UW_CHAT_UW_INBOX_HANDLER_H_

#include <map>
#include <string>

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/scoped_observation.h"
#include "base/task/cancelable_task_tracker.h"
#include "mojo/public/cpp/bindings/pending_receiver.h"
#include "mojo/public/cpp/bindings/pending_remote.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "uw/browser/agent/chat_alerts.h"
#include "uw/browser/agent/chat_index.h"
#include "uw/browser/agent/scheduled_chats.h"
#include "uw/browser/ui/webui/uw_chat/uw_chat.mojom.h"

class BrowserWindowInterface;
class Profile;

namespace uw {

class UwInboxUI;

// The Inbox's browser side: the profile's alerts, with each chat's title and,
// for a watched page, the page's icon, and what a click on one does. See
// docs/product.md, "Scheduled chats".
class UwInboxHandler : public mojom::InboxHandler,
                       public ChatAlerts::Observer,
                       public ChatIndex::Observer,
                       public ScheduledChats::Observer {
 public:
  UwInboxHandler(UwInboxUI* ui,
                 mojo::PendingRemote<mojom::InboxPage> page,
                 mojo::PendingReceiver<mojom::InboxHandler> receiver);
  UwInboxHandler(const UwInboxHandler&) = delete;
  UwInboxHandler& operator=(const UwInboxHandler&) = delete;
  ~UwInboxHandler() override;

  // mojom::InboxHandler:
  void ShowUI() override;
  void OpenAlert(const std::string& id) override;
  void OpenChat(const std::string& chat_id) override;
  void ClearAll() override;
  void ShowScheduled() override;
  void Close() override;

  // ChatAlerts::Observer:
  void OnAlertsChanged() override;

  // ChatIndex::Observer:
  void OnChatsChanged() override;

  // ScheduledChats::Observer:
  void OnScheduleChanged(const std::string& chat_id) override;

 private:
  Profile* profile() const;
  // The window the Inbox opened in, which a page or a chat opens in.
  BrowserWindowInterface* Window() const;
  // Remembers the icon of a watched page and sends the alerts.
  void OnLooked(const std::string& page,
                const std::string& title,
                const std::string& favicon);
  void SendAlerts();

  const raw_ptr<UwInboxUI> ui_;
  mojo::Remote<mojom::InboxPage> page_;
  mojo::Receiver<mojom::InboxHandler> receiver_;
  base::ScopedObservation<ChatAlerts, ChatAlerts::Observer> alerts_{this};
  base::ScopedObservation<ChatIndex, ChatIndex::Observer> index_{this};
  base::ScopedObservation<ScheduledChats, ScheduledChats::Observer> schedules_{
      this};
  // The icon of each watched page, by its address, once looked up.
  std::map<std::string, std::string> favicons_;
  base::CancelableTaskTracker tracker_;
  base::WeakPtrFactory<UwInboxHandler> weak_factory_{this};
};

}  // namespace uw

#endif  // UW_BROWSER_UI_WEBUI_UW_CHAT_UW_INBOX_HANDLER_H_
