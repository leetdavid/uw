// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/browser/ui/webui/uw_chat/uw_new_tab_handler.h"

#include <utility>

#include "base/functional/bind.h"
#include "base/strings/string_util.h"
#include "chrome/browser/profiles/profile.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_ui.h"
#include "ui/base/page_transition_types.h"
#include "uw/browser/agent/scheduled_chats.h"
#include "uw/browser/ai_providers/models.h"
#include "uw/browser/tab_organization/ai_connection.h"
#include "uw/browser/ui/webui/uw_chat/schedule_card.h"
#include "uw/components/ai_providers/providers.h"

namespace uw {

UwNewTabHandler::UwNewTabHandler(
    content::WebUI* web_ui,
    mojo::PendingRemote<mojom::NewTabPage> page,
    mojo::PendingReceiver<mojom::NewTabHandler> receiver)
    : web_ui_(web_ui),
      page_(std::move(page)),
      receiver_(this, std::move(receiver)) {
  prefs_.Init(profile()->GetPrefs());
  for (const char* pref :
       {kProvidersPref, kLanguageChoicePref, kAgentChoicePref}) {
    prefs_.Add(pref, base::BindRepeating(&UwNewTabHandler::SendBox,
                                         base::Unretained(this)));
  }
  SendBox();
}

UwNewTabHandler::~UwNewTabHandler() = default;

void UwNewTabHandler::NewSchedule(NewScheduleCallback callback) {
  const PrefService& prefs = *profile()->GetPrefs();
  const ScheduledChats* chats = ScheduledChats::For(profile());
  std::move(callback).Run(ScheduleStateOf(
      chats, {},
      DefaultSchedule(chats, AgentChoice(prefs),
                      ModeNamed(prefs.GetString(kAgentApprovalModePref)))));
}

void UwNewTabHandler::Ask(const std::string& text,
                          const std::string& provider_id,
                          const std::string& model) {
  ChatStart start;
  start.text = std::string(base::TrimWhitespaceASCII(text, base::TRIM_ALL));
  if (start.text.empty()) {
    return;
  }
  start.provider_id = provider_id;
  start.model = model;
  Become(ChatUrl(), std::move(start));
}

void UwNewTabHandler::AskOnSchedule(mojom::ChatSchedulePtr schedule) {
  ScheduledChats* chats = ScheduledChats::For(profile());
  std::optional<ScheduledChat> chat = ScheduledChatFrom(*schedule);
  if (!chats || !chat) {
    return;
  }
  chat->page.clear();
  chat->watch_for.clear();
  Become(ChatUrl(chats->Watch(std::move(*chat))), std::nullopt);
}

Profile* UwNewTabHandler::profile() const {
  return Profile::FromWebUI(web_ui_);
}

void UwNewTabHandler::SendBox() {
  auto box = mojom::NewTabBox::New();
  box->private_window = profile()->IsOffTheRecord();
  box->can_ask = AgentChoice(*profile()->GetPrefs()).has_value();
  page_->OnBox(std::move(box));
}

void UwNewTabHandler::Become(const GURL& url, std::optional<ChatStart> start) {
  content::WebContents* contents = web_ui_->GetWebContents();
  if (start) {
    SetChatStart(contents, std::move(*start));
  }
  // This handler goes with the page, so nothing of it is used after this.
  contents->GetController().LoadURL(url, content::Referrer(),
                                    ui::PAGE_TRANSITION_AUTO_BOOKMARK,
                                    std::string());
}

}  // namespace uw
