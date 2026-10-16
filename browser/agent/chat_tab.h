// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#ifndef UW_BROWSER_AGENT_CHAT_TAB_H_
#define UW_BROWSER_AGENT_CHAT_TAB_H_

#include <optional>
#include <string>

#include "components/tabs/public/tab_interface.h"

class BrowserWindowInterface;

namespace content {
class WebContents;
}

namespace uw {

// A uw command number, above every IDC_ value and the terminal's command.
inline constexpr int kNewChatCommand = 56101;

// The page of a chat is chrome://chat, shown as uw://chat.
inline constexpr char kChatHost[] = "chat";

// Opens a chat where a blank new tab would open.
void OpenChatTab(BrowserWindowInterface* window);

// What a chat opened by AskInNewChat starts with.
struct ChatStart {
  std::string text;
  // The tab handed to the agent as its tab 1.
  tabs::TabHandle tab;
};

// Opens a chat in front where a blank new tab would open, asks it `text`, and
// hands it `tab`, the tab the user was on, as the agent's tab 1.
void AskInNewChat(BrowserWindowInterface* window,
                  tabs::TabInterface* tab,
                  const std::string& text);

// What the chat in `contents` was opened with, taken once.
std::optional<ChatStart> TakeChatStart(content::WebContents* contents);

}  // namespace uw

#endif  // UW_BROWSER_AGENT_CHAT_TAB_H_
