// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/browser/ui/webui/uw_chat/uw_inbox_handler.h"

#include <algorithm>
#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/browser/ui/singleton_tabs.h"  // nogncheck
#include "chrome/browser/ui/webui/webui_embedding_context.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_ui.h"
#include "url/gurl.h"
#include "uw/browser/agent/chat_tab.h"
#include "uw/browser/ui/glass/glass.h"
#include "uw/browser/ui/webui/uw_chat/uw_inbox_ui.h"
#include "uw/browser/ui/webui/uw_chat/uw_scheduled_ui.h"
#include "uw/browser/ui/webui/uw_chat/uw_watch_card_handler.h"

namespace uw {

UwInboxHandler::UwInboxHandler(
    UwInboxUI* ui,
    mojo::PendingRemote<mojom::InboxPage> page,
    mojo::PendingReceiver<mojom::InboxHandler> receiver)
    : ui_(ui), page_(std::move(page)), receiver_(this, std::move(receiver)) {
  if (ChatAlerts* alerts = ChatAlerts::For(profile())) {
    alerts_.Observe(alerts);
  }
  if (ChatIndex* index = ChatIndex::For(profile())) {
    index_.Observe(index);
  }
  if (ScheduledChats* chats = ScheduledChats::For(profile())) {
    schedules_.Observe(chats);
  }
  SendAlerts();
}

UwInboxHandler::~UwInboxHandler() = default;

void UwInboxHandler::ShowUI() {
  if (auto embedder = ui_->embedder()) {
    embedder->ShowUI();
  }
}

void UwInboxHandler::OpenAlert(const std::string& id) {
  const ChatAlerts* alerts = alerts_.GetSource();
  if (!alerts) {
    return;
  }
  const auto alert = std::ranges::find(alerts->alerts(), id,
                                       &ChatAlerts::Alert::id);
  if (alert == alerts->alerts().end()) {
    return;
  }
  const ScheduledChat* chat = schedules_.GetSource()->Find(alert->chat_id);
  BrowserWindowInterface* window = Window();
  // An alert waiting for approval opens its chat, where the step is answered.
  if (alert->waiting || !chat || chat->page.empty() || !window) {
    OpenChat(alert->chat_id);
    return;
  }
  const GURL page(chat->page);
  if (tabs::TabInterface* tab = TabOfPage(profile(), page)) {
    GoToTab(tab);
  } else {
    chrome::AddTabAt(window, page, -1, true);
  }
  Close();
}

void UwInboxHandler::OpenChat(const std::string& chat_id) {
  const std::vector<tabs::TabInterface*> showing =
      TabsShowingChat(profile(), chat_id);
  if (!showing.empty()) {
    GoToTab(showing[0]);
  } else if (BrowserWindowInterface* window = Window()) {
    chrome::AddTabAt(window, ChatUrl(chat_id), -1, true);
  }
  Close();
}

void UwInboxHandler::ClearAll() {
  if (ChatAlerts* alerts = alerts_.GetSource()) {
    alerts->Clear();
  }
}

void UwInboxHandler::ShowScheduled() {
  if (BrowserWindowInterface* window = Window()) {
    ShowSingletonTab(window, GURL(kScheduledUrl));
  }
  Close();
}

void UwInboxHandler::Close() {
  if (auto embedder = ui_->embedder()) {
    embedder->CloseUI();
  }
}

void UwInboxHandler::OnAlertsChanged() {
  SendAlerts();
}

void UwInboxHandler::OnChatsChanged() {
  SendAlerts();
}

void UwInboxHandler::OnScheduleChanged(const std::string& chat_id) {
  SendAlerts();
}

Profile* UwInboxHandler::profile() const {
  return Profile::FromWebUI(ui_->web_ui());
}

BrowserWindowInterface* UwInboxHandler::Window() const {
  return webui::GetBrowserWindowInterface(ui_->web_ui()->GetWebContents());
}

void UwInboxHandler::OnLooked(const std::string& page,
                              const std::string& title,
                              const std::string& favicon) {
  favicons_[page] = favicon;
  SendAlerts();
}

void UwInboxHandler::SendAlerts() {
  const ChatAlerts* alerts = alerts_.GetSource();
  const ChatIndex* index = index_.GetSource();
  const ScheduledChats* chats = schedules_.GetSource();
  std::vector<mojom::InboxAlertPtr> sent;
  int scheduled = 0;
  if (!alerts || !index || !chats) {
    page_->OnAlerts(std::move(sent), scheduled, false);
    return;
  }
  std::map<std::string, std::string> titles;
  for (const ChatIndex::Entry& entry : index->Entries()) {
    titles[entry.id] = entry.title;
    if (chats->Find(entry.id) || chats->TriggerOf(entry.id)) {
      ++scheduled;
    }
  }
  for (const ChatAlerts::Alert& alert : alerts->alerts()) {
    auto shown = mojom::InboxAlert::New();
    shown->id = alert.id;
    shown->chat_id = alert.chat_id;
    shown->chat_title = titles[alert.chat_id];
    const ScheduledChat* chat = chats->Find(alert.chat_id);
    if (chat && !chat->page.empty()) {
      shown->page = chat->page;
      if (const auto favicon = favicons_.find(chat->page);
          favicon != favicons_.end()) {
        shown->favicon = favicon->second;
      } else {
        // Kept empty until the page is looked up, so that it is looked up
        // once.
        favicons_.try_emplace(chat->page);
        LookUpPage(profile(), GURL(chat->page), &tracker_,
                   base::BindOnce(&UwInboxHandler::OnLooked,
                                  weak_factory_.GetWeakPtr(), chat->page));
      }
    }
    shown->trigger = chats->TriggerOf(alert.chat_id) ||
                     (chat && !chat->trigger.empty());
    shown->what = alert.what;
    shown->text = alert.text;
    shown->time = alert.time.InMillisecondsFSinceUnixEpoch();
    shown->waiting = alert.waiting;
    sent.push_back(std::move(shown));
  }
  BrowserWindowInterface* window = Window();
  page_->OnAlerts(std::move(sent), scheduled,
                  window && ElementGlassOfWindow(window) > 0);
}

}  // namespace uw
