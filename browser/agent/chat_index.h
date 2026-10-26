// Copyright 2026 The uw Authors. All rights reserved.

#ifndef UW_BROWSER_AGENT_CHAT_INDEX_H_
#define UW_BROWSER_AGENT_CHAT_INDEX_H_

#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "base/files/file_path.h"
#include "base/files/important_file_writer.h"
#include "base/memory/weak_ptr.h"
#include "base/observer_list.h"
#include "base/observer_list_types.h"
#include "base/supports_user_data.h"
#include "base/time/time.h"

class Profile;

namespace uw {

// The saved chats of a profile, for the chat list: each chat's title, when it
// last changed, and what the list shows beside it. Kept in
// uw_chats/index.json beside the chats, so the list never reads every chat.
// A profile that kept chats before the index existed gets one built from its
// chat files.
class ChatIndex : public base::SupportsUserData::Data {
 public:
  enum class State { kDone, kRunning, kWaiting, kStopped, kUnread };

  struct Entry {
    std::string id;
    std::string title;
    base::Time updated;
    State state = State::kDone;
    // The uw command started it.
    bool from_terminal = false;
  };

  class Observer : public base::CheckedObserver {
   public:
    virtual void OnChatsChanged() = 0;
  };

  // The index of `profile`, made on first use. None in a private profile,
  // which keeps no chats.
  static ChatIndex* For(Profile* profile);

  explicit ChatIndex(const base::FilePath& profile_path);
  ChatIndex(const ChatIndex&) = delete;
  ChatIndex& operator=(const ChatIndex&) = delete;
  ~ChatIndex() override;

  bool loaded() const { return loaded_; }
  // Newest first.
  std::vector<Entry> Entries() const;
  void Update(Entry entry);
  // Forgets the chat and deletes its file. A chat deleted here is never
  // saved again by a page that still has it.
  void Delete(std::string_view id);
  bool WasDeleted(std::string_view id) const;

  void AddObserver(Observer* observer);
  void RemoveObserver(Observer* observer);

 private:
  void OnLoaded(std::vector<Entry> entries);
  void Save();

  const base::FilePath folder_;
  base::ImportantFileWriter writer_;
  bool loaded_ = false;
  std::map<std::string, Entry> entries_;
  std::set<std::string, std::less<>> deleted_;
  base::ObserverList<Observer> observers_;
  base::WeakPtrFactory<ChatIndex> weak_factory_{this};
};

}  // namespace uw

#endif  // UW_BROWSER_AGENT_CHAT_INDEX_H_
