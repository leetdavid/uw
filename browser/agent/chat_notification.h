// Copyright 2026 The uw Authors. All rights reserved.

#ifndef UW_BROWSER_AGENT_CHAT_NOTIFICATION_H_
#define UW_BROWSER_AGENT_CHAT_NOTIFICATION_H_

#include <optional>
#include <string>
#include <string_view>

#include "base/functional/callback.h"
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

// Asks the user to approve an outside agent's step from a notification, with
// Approve and Not this one: `title`, such as "claude-code waits for your
// approval", and the `step`. `answered` hears true or false from a button,
// or nothing from a click on the notification itself. See
// docs/plans/uw-command.md#the-tool-server.
using StepAnswered = base::RepeatingCallback<void(std::optional<bool>)>;
void AskAboutStep(Profile* profile,
                  const std::string& id,
                  const std::string& title,
                  const std::string& step,
                  StepAnswered answered);
// Takes back the notification `id`, if it still shows.
void CloseStepNotification(Profile* profile, const std::string& id);

// Brings uw forward on the chat whose notification was clicked, or answers
// a step's.
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
