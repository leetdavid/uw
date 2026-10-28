// Copyright 2026 The uw Authors. All rights reserved.

#ifndef UW_BROWSER_UI_WEBUI_UW_CHAT_UW_SCHEDULED_HANDLER_H_
#define UW_BROWSER_UI_WEBUI_UW_CHAT_UW_SCHEDULED_HANDLER_H_

#include <map>
#include <optional>
#include <string>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/scoped_observation.h"
#include "base/task/cancelable_task_tracker.h"
#include "base/values.h"
#include "mojo/public/cpp/bindings/pending_receiver.h"
#include "mojo/public/cpp/bindings/pending_remote.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "url/gurl.h"
#include "uw/browser/agent/chat_index.h"
#include "uw/browser/agent/scheduled_chats.h"
#include "uw/browser/ui/webui/uw_chat/uw_chat.mojom.h"

class Profile;

namespace content {
class WebUI;
}  // namespace content

namespace uw {

// The Scheduled page's browser side: the profile's scheduled chats, each
// with its runs and, for a watched page, the page's title and icon, and what
// the page's rows and cards do. See docs/product.md, "Scheduled chats".
class UwScheduledHandler : public mojom::ScheduledHandler,
                           public ScheduledChats::Observer,
                           public ChatIndex::Observer {
 public:
  UwScheduledHandler(content::WebUI* web_ui,
                     mojo::PendingRemote<mojom::ScheduledPage> page,
                     mojo::PendingReceiver<mojom::ScheduledHandler> receiver);
  UwScheduledHandler(const UwScheduledHandler&) = delete;
  UwScheduledHandler& operator=(const UwScheduledHandler&) = delete;
  ~UwScheduledHandler() override;

  // mojom::ScheduledHandler:
  void NewSchedule(NewScheduleCallback callback) override;
  void LookUpAddress(const std::string& address,
                     LookUpAddressCallback callback) override;
  void Watch(const std::string& chat_id,
             mojom::ChatSchedulePtr schedule) override;
  void StopWatching(const std::string& chat_id) override;
  void StartScheduledChat(mojom::ChatSchedulePtr schedule) override;
  void OpenPage(const std::string& chat_id) override;
  void OpenChat(const std::string& chat_id) override;
  void SetPaused(const std::string& chat_id, bool paused) override;
  void PauseAll(bool paused) override;

  // ScheduledChats::Observer:
  void OnScheduleChanged(const std::string& chat_id) override;

  // ChatIndex::Observer:
  void OnChatsChanged() override;

 private:
  // A page's title and its icon as a data: URL.
  struct Look {
    std::string title;
    std::string favicon;
  };

  Profile* profile() const;
  // The page shows a chat while it has a schedule or a running trigger.
  bool Listed(const std::string& chat_id) const;
  // Reads the runs of `chat_id` from its chat, then sends the rows.
  void LoadRuns(const std::string& chat_id);
  void OnRunsLoaded(const std::string& chat_id,
                    std::optional<base::DictValue> saved);
  // Remembers the title and icon of a watched page and sends the rows.
  void OnLooked(const std::string& page,
                const std::string& title,
                const std::string& favicon);
  void SendRows();

  const raw_ptr<content::WebUI> web_ui_;
  mojo::Remote<mojom::ScheduledPage> page_;
  mojo::Receiver<mojom::ScheduledHandler> receiver_;
  base::ScopedObservation<ScheduledChats, ScheduledChats::Observer> schedules_{
      this};
  base::ScopedObservation<ChatIndex, ChatIndex::Observer> index_{this};
  // The runs of each listed chat, as its chat keeps them, without steps.
  std::map<std::string, base::ListValue> runs_;
  // The title and icon of each watched page, by its address, once looked up.
  std::map<std::string, Look> looks_;
  base::CancelableTaskTracker tracker_;
  base::WeakPtrFactory<UwScheduledHandler> weak_factory_{this};
};

}  // namespace uw

#endif  // UW_BROWSER_UI_WEBUI_UW_CHAT_UW_SCHEDULED_HANDLER_H_
