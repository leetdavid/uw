// Copyright 2026 The uw Authors. All rights reserved.

#ifndef UW_BROWSER_AGENT_CHAT_ALERTS_H_
#define UW_BROWSER_AGENT_CHAT_ALERTS_H_

#include <string>
#include <string_view>
#include <vector>

#include "base/files/important_file_writer.h"
#include "base/memory/weak_ptr.h"
#include "base/observer_list.h"
#include "base/observer_list_types.h"
#include "base/supports_user_data.h"
#include "base/time/time.h"

class Profile;

namespace uw {

// A profile's alerts: a copy of every notification its scheduled chats sent,
// for the Inbox. Kept in uw_chats/alerts.json until the user clears them or
// deletes their chat. See docs/product.md, "Scheduled chats".
class ChatAlerts : public base::SupportsUserData::Data {
 public:
  struct Alert {
    std::string id;
    std::string chat_id;
    // What the notification said under the chat's title, such as "Change
    // alert", and its text.
    std::string what;
    std::string text;
    base::Time time;
    bool seen = false;
    // A step of the chat's run waits for the user's approval. The alert goes
    // once the step is answered or the run ends.
    bool waiting = false;
  };

  class Observer : public base::CheckedObserver {
   public:
    virtual void OnAlertsChanged() = 0;
  };

  // The alerts of `profile`, made on first use. None in a private profile,
  // which has no scheduled chats.
  static ChatAlerts* For(Profile* profile);

  explicit ChatAlerts(const base::FilePath& profile_path);
  ChatAlerts(const ChatAlerts&) = delete;
  ChatAlerts& operator=(const ChatAlerts&) = delete;
  ~ChatAlerts() override;

  bool loaded() const { return loaded_; }
  // Newest first.
  const std::vector<Alert>& alerts() const { return alerts_; }
  // How many alerts the user has not seen.
  size_t Unseen() const;

  void Add(const std::string& chat_id,
           const std::string& what,
           const std::string& text,
           bool waiting);
  // The Inbox opened, so every alert is seen.
  void SeeAll();
  // The notification of `chat_id` was clicked, so its newest alert is seen.
  void SeeNewest(std::string_view chat_id);
  void Clear();
  void RemoveChat(std::string_view chat_id);
  // The step a run of `chat_id` waited on was answered, or the run ended.
  void RemoveWaiting(std::string_view chat_id);

  void AddObserver(Observer* observer);
  void RemoveObserver(Observer* observer);

 private:
  void OnLoaded(std::vector<Alert> saved);
  // Saves the alerts and tells the observers.
  void Changed();

  base::ImportantFileWriter writer_;
  bool loaded_ = false;
  std::vector<Alert> alerts_;
  base::ObserverList<Observer> observers_;
  base::WeakPtrFactory<ChatAlerts> weak_factory_{this};
};

}  // namespace uw

#endif  // UW_BROWSER_AGENT_CHAT_ALERTS_H_
