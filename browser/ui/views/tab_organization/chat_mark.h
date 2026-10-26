// Copyright 2026 The uw Authors. All rights reserved.

#ifndef UW_BROWSER_UI_VIEWS_TAB_ORGANIZATION_CHAT_MARK_H_
#define UW_BROWSER_UI_VIEWS_TAB_ORGANIZATION_CHAT_MARK_H_

#include <optional>

#include "base/memory/raw_ptr.h"
#include "base/scoped_observation.h"
#include "components/tabs/public/tab_interface.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/views/view.h"
#include "uw/browser/agent/chat_index.h"

namespace views {
class Throbber;
}

namespace uw {

// How the chat a tab shows stands, at the end of the tab's row, as the chat
// page's list shows it: a spinner while the agent works, an amber dot while a
// step or a password waits for the user, a pause sign once it stopped early,
// and a dot for an answer not yet seen. Nothing for any other tab. See
// docs/plans/agent-chats.md.
class ChatMark : public views::View, public ChatIndex::Observer {
  METADATA_HEADER(ChatMark, views::View)

 public:
  explicit ChatMark(tabs::TabHandle tab);
  ChatMark(const ChatMark&) = delete;
  ChatMark& operator=(const ChatMark&) = delete;
  ~ChatMark() override;

  // Whether the tab's chat has a state to show.
  bool shown() const { return state_.has_value(); }
  std::optional<ChatIndex::State> state() const { return state_; }

  // views::View:
  void AddedToWidget() override;
  void RemovedFromWidget() override;
  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override;
  void OnPaint(gfx::Canvas* canvas) override;

  // ChatIndex::Observer:
  void OnChatsChanged() override;

 private:
  void Refresh();

  const tabs::TabHandle tab_;
  // Nothing while the chat is done, or the tab shows no chat.
  std::optional<ChatIndex::State> state_;
  raw_ptr<views::Throbber> spinner_ = nullptr;
  base::ScopedObservation<ChatIndex, ChatIndex::Observer> observation_{this};
};

}  // namespace uw

#endif  // UW_BROWSER_UI_VIEWS_TAB_ORGANIZATION_CHAT_MARK_H_
