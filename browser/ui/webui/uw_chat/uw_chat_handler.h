// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#ifndef UW_BROWSER_UI_WEBUI_UW_CHAT_UW_CHAT_HANDLER_H_
#define UW_BROWSER_UI_WEBUI_UW_CHAT_UW_CHAT_HANDLER_H_

#include <memory>
#include <string>

#include "base/memory/raw_ptr.h"
#include "mojo/public/cpp/bindings/pending_receiver.h"
#include "mojo/public/cpp/bindings/pending_remote.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "uw/browser/agent/agent_loop.h"
#include "uw/browser/ui/webui/uw_chat/uw_chat.mojom.h"

namespace content {
class WebContents;
}

namespace uw {

// Runs the agent of one chat and tells the chat's page each step. Owned by
// the page's controller, so the agent stops when the tab closes. Until
// approvals land, nothing here asks before a click or a keystroke.
class UwChatHandler : public mojom::ChatHandler {
 public:
  UwChatHandler(content::WebContents* contents,
                mojo::PendingRemote<mojom::ChatPage> page,
                mojo::PendingReceiver<mojom::ChatHandler> receiver);
  UwChatHandler(const UwChatHandler&) = delete;
  UwChatHandler& operator=(const UwChatHandler&) = delete;
  ~UwChatHandler() override;

  // mojom::ChatHandler:
  void Send(const std::string& text) override;

 private:
  // Starts the agent with the agent model in settings. False, after telling
  // the page why, when there is none or the page is not in a tab.
  bool Start();
  void OnStep(const AgentLoop::Step& step);

  const raw_ptr<content::WebContents> contents_;
  mojo::Remote<mojom::ChatPage> page_;
  mojo::Receiver<mojom::ChatHandler> receiver_;
  std::unique_ptr<AgentLoop> agent_;
};

}  // namespace uw

#endif  // UW_BROWSER_UI_WEBUI_UW_CHAT_UW_CHAT_HANDLER_H_
