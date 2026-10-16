// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#ifndef UW_BROWSER_AGENT_CHAT_NOTIFICATION_H_
#define UW_BROWSER_AGENT_CHAT_NOTIFICATION_H_

#include <optional>
#include <string>
#include <string_view>

#include "chrome/browser/notifications/notification_handler.h"

class Profile;

namespace uw {

// Sends the chat `id`'s macOS notification: the chat's `title`, then `what`
// happened, such as "Answered", then the start of `text`. The next one for
// the same chat replaces it. See the agent chats plan.
void NotifyAboutChat(Profile* profile,
                     std::string_view id,
                     const std::string& title,
                     const std::string& what,
                     const std::string& text);

// Brings uw forward on the chat whose notification was clicked.
class ChatNotificationHandler : public NotificationHandler {
 public:
  void OnClick(Profile* profile,
               const GURL& origin,
               const std::string& notification_id,
               const std::optional<int>& action_index,
               const std::optional<std::u16string>& reply,
               base::OnceClosure completed_closure) override;
};

}  // namespace uw

#endif  // UW_BROWSER_AGENT_CHAT_NOTIFICATION_H_
