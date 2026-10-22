// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#ifndef UW_BROWSER_UI_WEBUI_UW_CHAT_UW_WATCH_CARD_HANDLER_H_
#define UW_BROWSER_UI_WEBUI_UW_CHAT_UW_WATCH_CARD_HANDLER_H_

#include <string>

#include "base/memory/raw_ptr.h"
#include "base/scoped_observation.h"
#include "components/tabs/public/tab_interface.h"
#include "mojo/public/cpp/bindings/pending_receiver.h"
#include "mojo/public/cpp/bindings/pending_remote.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "url/gurl.h"
#include "uw/browser/agent/scheduled_chats.h"
#include "uw/browser/ui/webui/uw_chat/uw_chat.mojom.h"

namespace uw {

class UwWatchCardUI;

// The address of the watch card for `tab`'s page.
GURL WatchCardUrl(const tabs::TabInterface& tab);

// The watch card's browser side: the page of the tab its address names, and
// the scheduled chat that watches that page.
class UwWatchCardHandler : public mojom::WatchCardHandler,
                           public ScheduledChats::Observer {
 public:
  UwWatchCardHandler(UwWatchCardUI* ui,
                     mojo::PendingRemote<mojom::WatchCardPage> page,
                     mojo::PendingReceiver<mojom::WatchCardHandler> receiver);
  UwWatchCardHandler(const UwWatchCardHandler&) = delete;
  UwWatchCardHandler& operator=(const UwWatchCardHandler&) = delete;
  ~UwWatchCardHandler() override;

  // mojom::WatchCardHandler:
  void ShowUI() override;
  void Watch(mojom::ChatSchedulePtr schedule,
             const std::string& watch_for) override;
  void StopWatching() override;
  void OpenChat() override;
  void Close() override;

  // ScheduledChats::Observer:
  void OnScheduleChanged(const std::string& chat_id) override;

 private:
  // The tab whose page the card is about, or null once it closes.
  tabs::TabInterface* Tab() const;
  // The id of the chat that watches the tab's page, or empty.
  std::string Watcher() const;
  void SendCard();

  const raw_ptr<UwWatchCardUI> ui_;
  tabs::TabHandle tab_;
  mojo::Remote<mojom::WatchCardPage> page_;
  mojo::Receiver<mojom::WatchCardHandler> receiver_;
  base::ScopedObservation<ScheduledChats, ScheduledChats::Observer>
      observation_{this};
};

}  // namespace uw

#endif  // UW_BROWSER_UI_WEBUI_UW_CHAT_UW_WATCH_CARD_HANDLER_H_
