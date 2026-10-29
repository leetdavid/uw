// Copyright 2026 The uw Authors. All rights reserved.

#ifndef UW_BROWSER_UI_WEBUI_UW_CHAT_UW_WATCH_CARD_HANDLER_H_
#define UW_BROWSER_UI_WEBUI_UW_CHAT_UW_WATCH_CARD_HANDLER_H_

#include <map>
#include <string>
#include <vector>

#include "base/functional/callback_forward.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/scoped_observation.h"
#include "base/task/cancelable_task_tracker.h"
#include "base/values.h"
#include "components/tabs/public/tab_interface.h"
#include "mojo/public/cpp/bindings/pending_receiver.h"
#include "mojo/public/cpp/bindings/pending_remote.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "url/gurl.h"
#include "uw/browser/agent/scheduled_chats.h"
#include "uw/browser/ui/webui/uw_chat/uw_chat.mojom.h"

class Profile;

namespace content {
class WebContents;
}  // namespace content

namespace uw {

class UwWatchCardUI;

// The address of the watch card for `tab`'s page.
GURL WatchCardUrl(const tabs::TabInterface& tab);

// The icon of the page `contents` shows, as a data: URL, or empty.
std::string FaviconOf(content::WebContents* contents);

// A tab of `page`, matched by WatchedAddress(), in one of `profile`'s windows
// the user sees, or null.
tabs::TabInterface* TabOfPage(Profile* profile, const GURL& page);

// Gives `done` the title of `page` and its icon as a data: URL, from a tab of
// it, else from history, or empty when uw knows neither. `tracker` cancels a
// lookup still under way.
void LookUpPage(Profile* profile,
                const GURL& page,
                base::CancelableTaskTracker* tracker,
                base::OnceCallback<void(const std::string& title,
                                        const std::string& favicon)> done);

// The watch card's browser side: the page of the tab its address names, and
// the scheduled chats that watch that page.
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
  void Watch(const std::string& chat_id,
             mojom::ChatSchedulePtr schedule) override;
  void StopWatching(const std::string& chat_id) override;
  void CheckNow(const std::string& chat_id) override;
  void OpenChat(const std::string& chat_id) override;
  void Close() override;

  // ScheduledChats::Observer:
  void OnScheduleChanged(const std::string& chat_id) override;

 private:
  // The tab whose page the card is about, or null once it closes.
  tabs::TabInterface* Tab() const;
  // The ids of the chats that watch the tab's page.
  std::vector<std::string> Watchers() const;
  // Reads each watch's runs from its chat, then sends the card.
  void LoadRuns();
  void OnRunsLoaded(const std::string& chat_id,
                    std::optional<base::DictValue> saved);
  void SendCard();

  const raw_ptr<UwWatchCardUI> ui_;
  tabs::TabHandle tab_;
  mojo::Remote<mojom::WatchCardPage> page_;
  mojo::Receiver<mojom::WatchCardHandler> receiver_;
  base::ScopedObservation<ScheduledChats, ScheduledChats::Observer>
      observation_{this};
  // The lines of each watch's runs of the last 14 days, as its chat keeps
  // them.
  std::map<std::string, base::ListValue> runs_;
  base::WeakPtrFactory<UwWatchCardHandler> weak_factory_{this};
};

}  // namespace uw

#endif  // UW_BROWSER_UI_WEBUI_UW_CHAT_UW_WATCH_CARD_HANDLER_H_
