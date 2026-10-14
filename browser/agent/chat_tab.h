// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#ifndef UW_BROWSER_AGENT_CHAT_TAB_H_
#define UW_BROWSER_AGENT_CHAT_TAB_H_

class BrowserWindowInterface;

namespace uw {

// A uw command number, above every IDC_ value and the terminal's command.
inline constexpr int kNewChatCommand = 56101;

// The page of a chat is chrome://chat, shown as uw://chat.
inline constexpr char kChatHost[] = "chat";

// Opens a chat where a blank new tab would open.
void OpenChatTab(BrowserWindowInterface* window);

}  // namespace uw

#endif  // UW_BROWSER_AGENT_CHAT_TAB_H_
