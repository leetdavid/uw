// Copyright 2026 The uw Authors. All rights reserved.

#ifndef UW_BROWSER_AGENT_FAKE_CHAT_H_
#define UW_BROWSER_AGENT_FAKE_CHAT_H_

#include "base/memory/weak_ptr.h"
#include "uw/browser/agent/chat_tab.h"

namespace content {
class WebContents;
}

namespace uw {

// Makes a page tell the closes that would stop a chat that it is one, as
// uw://chat does, so a test needs no model.
class FakeChat : public ChatWorkSource {
 public:
  FakeChat(content::WebContents* page, ChatWork work);
  FakeChat(const FakeChat&) = delete;
  FakeChat& operator=(const FakeChat&) = delete;
  ~FakeChat() override;

  void set_work(ChatWork work) { work_ = work; }

  // ChatWorkSource:
  ChatWork GetChatWork() const override;

 private:
  ChatWork work_;
  base::WeakPtrFactory<FakeChat> weak_factory_{this};
};

}  // namespace uw

#endif  // UW_BROWSER_AGENT_FAKE_CHAT_H_
