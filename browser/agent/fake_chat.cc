// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/browser/agent/fake_chat.h"

namespace uw {

FakeChat::FakeChat(content::WebContents* page, ChatWork work) : work_(work) {
  SetChatWorkSource(page, weak_factory_.GetWeakPtr());
}

FakeChat::~FakeChat() = default;

ChatWork FakeChat::GetChatWork() const {
  return work_;
}

}  // namespace uw
