// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/browser/agent/chat_notification.h"

#include <utility>
#include <vector>

#include "base/strings/strcat.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/notifications/notification_display_service.h"
#include "chrome/browser/notifications/notification_display_service_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "components/tabs/public/tab_interface.h"
#include "ui/base/models/image_model.h"
#include "ui/gfx/text_elider.h"
#include "ui/message_center/public/cpp/notification.h"
#include "ui/message_center/public/cpp/notifier_id.h"
#include "url/gurl.h"
#include "uw/browser/agent/chat_tab.h"

namespace uw {
namespace {

// Enough for the lines macOS shows; it cuts the rest itself.
constexpr size_t kMaxTextLength = 200;

}  // namespace

void NotifyAboutChat(Profile* profile,
                     std::string_view id,
                     const std::string& title,
                     const std::string& what,
                     const std::string& text) {
  message_center::RichNotificationData data;
  // Each answer is news, even when it replaces the last one.
  data.renotify = true;
  message_center::Notification notification(
      message_center::NOTIFICATION_TYPE_SIMPLE, base::StrCat({"uw-chat-", id}),
      base::UTF8ToUTF16(title.empty() ? "Chat" : title),
      gfx::TruncateString(
          base::CollapseWhitespace(base::UTF8ToUTF16(text), false),
          kMaxTextLength, gfx::WORD_BREAK),
      ui::ImageModel(), u"uw", ChatUrl(id),
      message_center::NotifierId(message_center::NotifierType::SYSTEM_COMPONENT,
                                 "uw_chat"),
      data, /*delegate=*/nullptr);
  // Shown under the title instead of the chat's address.
  notification.set_context_message(base::UTF8ToUTF16(what));
  NotificationDisplayServiceFactory::GetForProfile(profile)->Display(
      NotificationHandler::Type::UW_CHAT, notification, /*metadata=*/nullptr);
}

void ChatNotificationHandler::OnClick(
    Profile* profile,
    const GURL& origin,
    const std::string& notification_id,
    const std::optional<int>& action_index,
    const std::optional<std::u16string>& reply,
    base::OnceClosure completed_closure) {
  const std::vector<tabs::TabInterface*> showing =
      TabsShowingChat(profile, origin.path().substr(1));
  if (!showing.empty()) {
    GoToTab(showing[0]);
  }
  NotificationDisplayServiceFactory::GetForProfile(profile)->Close(
      NotificationHandler::Type::UW_CHAT, notification_id);
  std::move(completed_closure).Run();
}

}  // namespace uw
