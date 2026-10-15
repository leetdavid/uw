// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#ifndef UW_BROWSER_UI_WEBUI_UW_CHAT_UW_CHAT_HANDLER_H_
#define UW_BROWSER_UI_WEBUI_UW_CHAT_UW_CHAT_HANDLER_H_

#include <map>
#include <memory>
#include <optional>
#include <string>

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/values.h"
#include "mojo/public/cpp/bindings/pending_receiver.h"
#include "mojo/public/cpp/bindings/pending_remote.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "uw/browser/agent/agent_loop.h"
#include "uw/browser/agent/chat_file.h"
#include "uw/browser/ui/webui/uw_chat/uw_chat.mojom.h"

namespace content {
class WebContents;
}

namespace uw {

// Runs the agent of one chat, tells the chat's page each step, and saves the
// chat after every change. Owned by the page's controller, so the agent stops
// when the tab closes. Until approvals land, nothing here asks before a click
// or a keystroke.
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
  void Resume() override;

 private:
  void OnLoaded(std::optional<base::DictValue> saved);
  // Starts the agent with the agent model in settings, giving it back the
  // tabs it had. False, after telling the page why, when there is no agent
  // model or the page is not in a tab.
  bool Start();
  void OnStep(const AgentLoop::Step& step);
  bool Working() const;
  // One line of the saved log as the page shows it.
  mojom::StepPtr LineFor(const base::DictValue& entry) const;
  void Log(base::DictValue entry);
  void Save();

  const raw_ptr<content::WebContents> contents_;
  mojo::Remote<mojom::ChatPage> page_;
  mojo::Receiver<mojom::ChatHandler> receiver_;
  std::string id_;
  // None in a private window, where nothing is kept.
  std::unique_ptr<ChatFile> file_;
  bool loaded_ = false;
  std::string title_;
  // What the page shows, in order. Steps are kept in the context.
  base::ListValue log_;
  // The saved conversation and the agent's tabs by number, by the tree's page
  // ids, until the agent starts again.
  std::unique_ptr<AgentContext> saved_context_;
  std::map<int, std::string> saved_tabs_;
  std::unique_ptr<AgentLoop> agent_;
  base::WeakPtrFactory<UwChatHandler> weak_factory_{this};
};

}  // namespace uw

#endif  // UW_BROWSER_UI_WEBUI_UW_CHAT_UW_CHAT_HANDLER_H_
