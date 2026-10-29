// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/browser/ui/webui/uw_chat/uw_scheduled_handler.h"

#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "base/strings/strcat.h"
#include "base/strings/string_util.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "components/prefs/pref_service.h"
#include "components/tabs/public/tab_interface.h"
#include "components/url_formatter/url_formatter.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_ui.h"
#include "uw/browser/agent/chat_file.h"
#include "uw/browser/agent/chat_tab.h"
#include "uw/browser/ai_providers/models.h"
#include "uw/browser/tab_organization/ai_connection.h"
#include "uw/browser/ui/webui/uw_chat/schedule_card.h"
#include "uw/browser/ui/webui/uw_chat/uw_watch_card_handler.h"

namespace uw {
namespace {

// What Watch a page watches for an address typed into it: the address with
// https:// added when it was left out, as WatchedAddress() keeps it, or an
// invalid address when it names no web page.
GURL AddressFrom(std::string_view typed) {
  std::string text(base::TrimWhitespaceASCII(typed, base::TRIM_ALL));
  if (text.empty() ||
      text.find_first_of(base::kWhitespaceASCII) != std::string::npos) {
    return GURL();
  }
  if (text.find("://") == std::string::npos) {
    text = base::StrCat({"https://", text});
  }
  const GURL url(text);
  if (!url.is_valid() || !url.SchemeIsHTTPOrHTTPS() ||
      url.host().find('.') == std::string_view::npos) {
    return GURL();
  }
  return WatchedAddress(url);
}

std::string HostOf(const GURL& page) {
  return url_formatter::StripWWW(std::string(page.host()));
}

}  // namespace

UwScheduledHandler::UwScheduledHandler(
    content::WebUI* web_ui,
    mojo::PendingRemote<mojom::ScheduledPage> page,
    mojo::PendingReceiver<mojom::ScheduledHandler> receiver)
    : web_ui_(web_ui),
      page_(std::move(page)),
      receiver_(this, std::move(receiver)) {
  if (ScheduledChats* chats = ScheduledChats::For(profile())) {
    schedules_.Observe(chats);
  }
  if (ChatIndex* index = ChatIndex::For(profile())) {
    index_.Observe(index);
  }
  OnChatsChanged();
}

UwScheduledHandler::~UwScheduledHandler() = default;

void UwScheduledHandler::NewSchedule(NewScheduleCallback callback) {
  const PrefService& prefs = *profile()->GetPrefs();
  const ScheduledChats* chats = schedules_.GetSource();
  std::move(callback).Run(ScheduleStateOf(
      chats, {},
      DefaultSchedule(chats, AgentChoice(prefs),
                      ModeNamed(prefs.GetString(kAgentApprovalModePref)))));
}

void UwScheduledHandler::LookUpAddress(const std::string& address,
                                       LookUpAddressCallback callback) {
  const GURL page = AddressFrom(address);
  auto found = mojom::WatchAddress::New();
  const ScheduledChats* chats = schedules_.GetSource();
  if (!page.is_valid() || !chats) {
    std::move(callback).Run(std::move(found));
    return;
  }
  found->page = page.spec();
  found->host = HostOf(page);
  if (const std::string* id = chats->ChatWatching(page)) {
    found->chat_id = *id;
  }
  LookUpPage(
      profile(), page, &tracker_,
      base::BindOnce(
          [](mojom::WatchAddressPtr found, LookUpAddressCallback callback,
             const std::string& title, const std::string& favicon) {
            found->title = title;
            found->favicon = favicon;
            std::move(callback).Run(std::move(found));
          },
          std::move(found), std::move(callback)));
}

void UwScheduledHandler::Watch(const std::string& chat_id,
                               mojom::ChatSchedulePtr schedule) {
  ScheduledChats* chats = schedules_.GetSource();
  const GURL address = AddressFrom(schedule->page);
  std::optional<ScheduledChat> chat = ScheduledChatFrom(*schedule);
  if (!chats || !address.is_valid() || !chat) {
    return;
  }
  chat->page = address.spec();
  if (chat_id.empty()) {
    chats->Watch(std::move(*chat));
  } else if (chats->Find(chat_id)) {
    chats->Save(chat_id, std::move(*chat));
  }
}

void UwScheduledHandler::StopWatching(const std::string& chat_id) {
  ScheduledChats* chats = schedules_.GetSource();
  if (chats && chats->Find(chat_id)) {
    chats->Remove(chat_id);
  }
}

void UwScheduledHandler::StartScheduledChat(mojom::ChatSchedulePtr schedule) {
  ScheduledChats* chats = schedules_.GetSource();
  std::optional<ScheduledChat> chat = ScheduledChatFrom(*schedule);
  if (chats && chat) {
    chat->page.clear();
    chat->watch_for.clear();
    chats->Watch(std::move(*chat));
  }
}

void UwScheduledHandler::OpenPage(const std::string& chat_id) {
  const ScheduledChats* chats = schedules_.GetSource();
  const ScheduledChat* chat = chats ? chats->Find(chat_id) : nullptr;
  if (!chat || chat->page.empty()) {
    OpenChat(chat_id);
    return;
  }
  const GURL page(chat->page);
  if (tabs::TabInterface* tab = TabOfPage(profile(), page)) {
    GoToTab(tab);
  } else if (tabs::TabInterface* own = tabs::TabInterface::MaybeGetFromContents(
                 web_ui_->GetWebContents())) {
    chrome::AddTabAt(own->GetBrowserWindowInterface(), page, -1, true);
  }
}

void UwScheduledHandler::OpenChat(const std::string& chat_id) {
  const std::vector<tabs::TabInterface*> showing =
      TabsShowingChat(profile(), chat_id);
  if (!showing.empty()) {
    GoToTab(showing[0]);
  } else if (tabs::TabInterface* own = tabs::TabInterface::MaybeGetFromContents(
                 web_ui_->GetWebContents())) {
    chrome::AddTabAt(own->GetBrowserWindowInterface(), ChatUrl(chat_id), -1,
                     true);
  }
}

void UwScheduledHandler::SetPaused(const std::string& chat_id, bool paused) {
  if (ScheduledChats* chats = schedules_.GetSource()) {
    chats->SetPaused(chat_id, paused);
  }
}

void UwScheduledHandler::CheckNow(const std::string& chat_id) {
  if (ScheduledChats* chats = schedules_.GetSource()) {
    chats->CheckNow(chat_id);
  }
}

void UwScheduledHandler::PauseAll(bool paused) {
  if (ScheduledChats* chats = schedules_.GetSource()) {
    chats->SetAllPaused(paused);
  }
}

void UwScheduledHandler::OnScheduleChanged(const std::string& chat_id) {
  if (!Listed(chat_id)) {
    runs_.erase(chat_id);
    SendRows();
    return;
  }
  LoadRuns(chat_id);
}

void UwScheduledHandler::OnChatsChanged() {
  std::erase_if(runs_,
                [this](const auto& runs) { return !Listed(runs.first); });
  if (const ChatIndex* index = index_.GetSource()) {
    for (const ChatIndex::Entry& entry : index->Entries()) {
      if (Listed(entry.id) && !runs_.contains(entry.id)) {
        LoadRuns(entry.id);
      }
    }
  }
  SendRows();
}

Profile* UwScheduledHandler::profile() const {
  return Profile::FromWebUI(web_ui_);
}

bool UwScheduledHandler::Listed(const std::string& chat_id) const {
  const ScheduledChats* chats = schedules_.GetSource();
  return chats && (chats->Find(chat_id) || chats->TriggerOf(chat_id));
}

void UwScheduledHandler::LoadRuns(const std::string& chat_id) {
  // Kept empty until the chat is read, so that it is read once.
  runs_.try_emplace(chat_id);
  const ScheduledChat* chat = schedules_.GetSource()->Find(chat_id);
  if (chat && !chat->page.empty() && !looks_.contains(chat->page)) {
    looks_.try_emplace(chat->page);
    LookUpPage(profile(), GURL(chat->page), &tracker_,
               base::BindOnce(&UwScheduledHandler::OnLooked,
                              weak_factory_.GetWeakPtr(), chat->page));
  }
  // A run's lines go into the chat file on its sequence before the schedule
  // says it changed, so this reads them.
  ChatFile(profile()->GetPath(), chat_id)
      .Load(base::BindOnce(&UwScheduledHandler::OnRunsLoaded,
                           weak_factory_.GetWeakPtr(), chat_id));
}

void UwScheduledHandler::OnRunsLoaded(const std::string& chat_id,
                                      std::optional<base::DictValue> saved) {
  if (!Listed(chat_id)) {
    return;
  }
  base::ListValue runs;
  const base::ListValue* log = saved ? saved->FindList("log") : nullptr;
  if (log) {
    for (const base::Value& entry : *log) {
      const base::DictValue* line = entry.GetIfDict();
      const std::string* kind = line ? line->FindString("kind") : nullptr;
      if (kind && *kind == "run") {
        runs.Append(line->Clone().Set("steps", base::ListValue()));
      }
    }
  }
  runs_[chat_id] = std::move(runs);
  SendRows();
}

void UwScheduledHandler::OnLooked(const std::string& page,
                                  const std::string& title,
                                  const std::string& favicon) {
  looks_[page] = {title, favicon};
  SendRows();
}

void UwScheduledHandler::SendRows() {
  const ScheduledChats* chats = schedules_.GetSource();
  const ChatIndex* index = index_.GetSource();
  std::vector<mojom::ScheduledRowPtr> rows;
  if (!chats || !index) {
    page_->OnRows(std::move(rows));
    return;
  }
  for (const ChatIndex::Entry& entry : index->Entries()) {
    if (!Listed(entry.id)) {
      continue;
    }
    auto row = mojom::ScheduledRow::New();
    row->title = entry.title;
    auto watch = mojom::PageWatch::New();
    watch->chat_id = entry.id;
    const ScheduledChat* chat = chats->Find(entry.id);
    row->scheduled = chat != nullptr;
    watch->state =
        ScheduleStateOf(chats, entry.id, chat ? *chat : ScheduledChat());
    if (chat && !chat->page.empty()) {
      row->host = HostOf(GURL(chat->page));
      if (const auto look = looks_.find(chat->page); look != looks_.end()) {
        row->page_title = look->second.title;
        row->favicon = look->second.favicon;
      }
    }
    if (const auto runs = runs_.find(entry.id); runs != runs_.end()) {
      for (const base::Value& line : runs->second) {
        auto run = mojom::Step::New();
        run->kind = mojom::StepKind::kRun;
        if (const std::string* text = line.GetDict().FindString("text")) {
          run->text = *text;
        }
        run->run = RunLineFrom(line.GetDict());
        watch->runs.push_back(std::move(run));
      }
    }
    row->watch = std::move(watch);
    if (const ChatTriggers::Trigger* trigger = chats->TriggerOf(entry.id)) {
      row->trigger = trigger->command;
    }
    rows.push_back(std::move(row));
  }
  page_->OnRows(std::move(rows));
}

}  // namespace uw
