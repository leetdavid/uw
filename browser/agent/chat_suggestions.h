// Copyright 2026 The uw Authors. All rights reserved.

#ifndef UW_BROWSER_AGENT_CHAT_SUGGESTIONS_H_
#define UW_BROWSER_AGENT_CHAT_SUGGESTIONS_H_

#include <memory>
#include <string>
#include <vector>

#include "base/files/file_path.h"
#include "base/files/important_file_writer.h"
#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/supports_user_data.h"
#include "base/task/cancelable_task_tracker.h"
#include "base/time/time.h"
#include "components/history/core/browser/history_types.h"
#include "uw/browser/ai_providers/remote_model_client.h"

class Profile;

namespace uw {

// The requests a new chat suggests. The language model writes them from the
// titles of recent chats and the sites visited most this week, once a day,
// and they are kept in uw_chats/suggestions.json. A private profile has none,
// and neither does a profile without a language model or anything to go on:
// the chat page then shows the product's examples.
class ChatSuggestions : public base::SupportsUserData::Data {
 public:
  using Callback = base::OnceCallback<void(const std::vector<std::string>&)>;

  // How old the suggestions get before they are written again.
  static constexpr base::TimeDelta kLifetime = base::Days(1);

  // The suggestions of `profile`, made on first use. None in a private
  // profile.
  static ChatSuggestions* For(Profile* profile);

  explicit ChatSuggestions(Profile* profile);
  ChatSuggestions(const ChatSuggestions&) = delete;
  ChatSuggestions& operator=(const ChatSuggestions&) = delete;
  ~ChatSuggestions() override;

  // Answers with the suggestions written last, empty when there are none,
  // once they are read from disk. Asks for new ones when they are due.
  void Get(Callback callback);

 private:
  struct Saved {
    std::vector<std::string> suggestions;
    base::Time written;
  };
  static Saved Load(const base::FilePath& file);

  void OnLoaded(Saved saved);
  void RefreshIfDue();
  void OnSites(history::MostVisitedURLList sites);
  void OnAnswer(RemoteModelClient::Outcome outcome);

  raw_ptr<Profile> profile_;
  bool loaded_ = false;
  std::vector<Callback> waiting_;
  Saved saved_;
  // When the model was last asked, to wait a while after a request that gave
  // nothing.
  base::Time tried_;
  bool refreshing_ = false;
  base::CancelableTaskTracker history_tasks_;
  std::unique_ptr<RemoteModelClient> client_;
  base::ImportantFileWriter writer_;
  base::WeakPtrFactory<ChatSuggestions> weak_factory_{this};
};

}  // namespace uw

#endif  // UW_BROWSER_AGENT_CHAT_SUGGESTIONS_H_
