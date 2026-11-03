// Copyright 2026 The uw Authors. All rights reserved.

#ifndef UW_BROWSER_AGENT_CHAT_TAB_H_
#define UW_BROWSER_AGENT_CHAT_TAB_H_

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/memory/weak_ptr.h"
#include "components/tabs/public/tab_interface.h"
#include "url/gurl.h"

class BrowserWindowInterface;
class Profile;

namespace content {
class WebContents;
}

namespace uw {

// A uw command number, above every IDC_ value and the terminal's command.
inline constexpr int kNewChatCommand = 56101;

// The page of a chat is chrome://chat, shown as uw://chat.
inline constexpr char kChatHost[] = "chat";

// The address of the chat `id`, or of a new chat when `id` is empty.
GURL ChatUrl(std::string_view id = {});

// The tabs of `profile`, in every window, that show the chat `id`. A tab
// that session restore has not loaded yet counts by the address it will load.
std::vector<tabs::TabInterface*> TabsShowingChat(Profile* profile,
                                                 std::string_view id);

// The chat tab of `window` the user was on last, or null when it has none.
tabs::TabInterface* LastChatTab(BrowserWindowInterface* window);

// Selects `tab` and brings its window, and uw, to the front.
void GoToTab(tabs::TabInterface* tab);

// Opens a chat where a blank new tab would open.
void OpenChatTab(BrowserWindowInterface* window);

// What a chat opened by AskInNewChat starts with.
struct ChatStart {
  std::string text;
  // The tab handed to the agent as its tab 1.
  tabs::TabHandle tab;
  // The uw command started it. See docs/plans/uw-command.md#chats.
  bool from_terminal = false;
  // The model the new tab page's model chip picked, or empty for the agent
  // model.
  std::string provider_id;
  std::string model;
};

// Gives the chat `chat` will show what it starts with, for TakeChatStart.
void SetChatStart(content::WebContents* chat, ChatStart start);

// Opens a chat in front where a blank new tab would open, asks it `text`, and
// hands it `tab`, if any, as the agent's tab 1.
void AskInNewChat(BrowserWindowInterface* window,
                  tabs::TabInterface* tab,
                  const std::string& text);

// What the chat in `contents` was opened with, taken once.
std::optional<ChatStart> TakeChatStart(content::WebContents* contents);

// What a chat's agent is doing, for a close that would stop it. A chat
// works from the user's message until its answer, and while a step waits for
// the user. See docs/plans/agent-chats.md#closing-a-working-chat.
enum class ChatWork { kIdle, kWorking, kWaitsForApproval, kWaitsForPassword };

// A chat page, as the closes that would stop it see it.
class ChatWorkSource {
 public:
  virtual ChatWork GetChatWork() const = 0;

 protected:
  virtual ~ChatWorkSource() = default;
};

// The chat page in `contents` tells what its agent does through `source`.
void SetChatWorkSource(content::WebContents* contents,
                       base::WeakPtr<ChatWorkSource> source);

// What the agent of the chat page in `contents` does. kIdle for any other
// page, and for a chat out of sight in the run window.
ChatWork ChatWorkOf(content::WebContents* contents);

// Whether a close of `contents` would stop a working chat.
bool IsWorkingChat(content::WebContents* contents);

}  // namespace uw

#endif  // UW_BROWSER_AGENT_CHAT_TAB_H_
