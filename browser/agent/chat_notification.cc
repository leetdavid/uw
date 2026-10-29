// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/browser/agent/chat_notification.h"

#include <map>
#include <utility>
#include <vector>

#include "base/no_destructor.h"
#include "base/strings/strcat.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/notifications/notification_display_service.h"
#include "chrome/browser/notifications/notification_display_service_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/navigator/browser_navigator.h"
#include "chrome/browser/ui/navigator/browser_navigator_params.h"
#include "components/tabs/public/tab_interface.h"
#include "ui/base/models/image_model.h"
#include "ui/base/page_transition_types.h"
#include "ui/base/window_open_disposition.h"
#include "ui/gfx/text_elider.h"
#include "ui/message_center/public/cpp/notification.h"
#include "ui/message_center/public/cpp/notifier_id.h"
#include "url/gurl.h"
#include "uw/browser/agent/chat_alerts.h"
#include "uw/browser/agent/chat_file.h"
#include "uw/browser/agent/chat_tab.h"

namespace uw {
namespace {

// Enough for the lines macOS shows; it cuts the rest itself.
constexpr size_t kMaxTextLength = 200;

// Who hears the answer to each step's notification, by its id.
std::map<std::string, StepAnswered>& StepsAsked() {
  static base::NoDestructor<std::map<std::string, StepAnswered>> asked;
  return *asked;
}

std::string ChatNotificationId(std::string_view chat_id) {
  return base::StrCat({"uw-chat-", chat_id});
}

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
      message_center::NOTIFICATION_TYPE_SIMPLE, ChatNotificationId(id),
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

void CloseChatNotification(Profile* profile, std::string_view id) {
  NotificationDisplayServiceFactory::GetForProfile(profile)->Close(
      NotificationHandler::Type::UW_CHAT, ChatNotificationId(id));
}

bool StaysAfterQuit(NotificationHandler::Type type,
                    const message_center::Notification& notification) {
  return type == NotificationHandler::Type::UW_CHAT &&
         notification.buttons().empty();
}

void AskAboutStep(Profile* profile,
                  const std::string& id,
                  const std::string& title,
                  const std::string& step,
                  StepAnswered answered) {
  message_center::RichNotificationData data;
  data.buttons.emplace_back(u"Approve");
  data.buttons.emplace_back(u"Not this one");
  message_center::Notification notification(
      message_center::NOTIFICATION_TYPE_SIMPLE, id, base::UTF8ToUTF16(title),
      gfx::TruncateString(base::UTF8ToUTF16(step), kMaxTextLength,
                          gfx::WORD_BREAK),
      ui::ImageModel(), u"uw", ChatUrl(),
      message_center::NotifierId(message_center::NotifierType::SYSTEM_COMPONENT,
                                 "uw_chat"),
      data, /*delegate=*/nullptr);
  StepsAsked()[id] = std::move(answered);
  NotificationDisplayServiceFactory::GetForProfile(profile)->Display(
      NotificationHandler::Type::UW_CHAT, notification, /*metadata=*/nullptr);
}

void CloseStepNotification(Profile* profile, const std::string& id) {
  if (StepsAsked().erase(id)) {
    NotificationDisplayServiceFactory::GetForProfile(profile)->Close(
        NotificationHandler::Type::UW_CHAT, id);
  }
}

void ChatNotificationHandler::OnClick(
    Profile* profile,
    const GURL& origin,
    const std::string& notification_id,
    const std::optional<int>& action_index,
    const std::optional<std::u16string>& reply,
    base::OnceClosure completed_closure) {
  if (auto asked = StepsAsked().find(notification_id);
      asked != StepsAsked().end()) {
    StepAnswered answered = std::move(asked->second);
    CloseStepNotification(profile, notification_id);
    answered.Run(action_index ? std::optional<bool>(*action_index == 0)
                              : std::nullopt);
    std::move(completed_closure).Run();
    return;
  }
  const std::string chat_id(origin.path().substr(1));
  if (ChatAlerts* alerts = ChatAlerts::For(profile)) {
    alerts->SeeNewest(chat_id);
  }
  const std::vector<tabs::TabInterface*> showing =
      TabsShowingChat(profile, chat_id);
  if (!showing.empty()) {
    GoToTab(showing[0]);
  } else if (ChatFile::IsChatId(chat_id)) {
    // No tab shows the chat, as after uw restarts, so it opens in one.
    NavigateParams params(profile, ChatUrl(chat_id),
                          ui::PAGE_TRANSITION_AUTO_TOPLEVEL);
    params.disposition = WindowOpenDisposition::NEW_FOREGROUND_TAB;
    Navigate(&params);
    if (params.navigated_or_inserted_contents) {
      GoToTab(tabs::TabInterface::GetFromContents(
          params.navigated_or_inserted_contents));
    }
  }
  NotificationDisplayServiceFactory::GetForProfile(profile)->Close(
      NotificationHandler::Type::UW_CHAT, notification_id);
  std::move(completed_closure).Run();
}

}  // namespace uw
