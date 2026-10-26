// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/browser/ui/webui/uw_chat/uw_watch_card_handler.h"

#include <optional>
#include <utility>
#include <vector>

#include "base/base64.h"
#include "base/functional/bind.h"
#include "base/json/values_util.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "components/favicon/content/content_favicon_driver.h"
#include "components/prefs/pref_service.h"
#include "components/url_formatter/url_formatter.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_ui.h"
#include "net/base/url_util.h"
#include "ui/gfx/codec/png_codec.h"
#include "ui/gfx/image/image.h"
#include "uw/browser/agent/chat_file.h"
#include "uw/browser/agent/chat_tab.h"
#include "uw/browser/ai_providers/models.h"
#include "uw/browser/tab_organization/ai_connection.h"
#include "uw/browser/ui/glass/glass.h"
#include "uw/browser/ui/webui/uw_chat/schedule_card.h"
#include "uw/browser/ui/webui/uw_chat/uw_watch_card_ui.h"

namespace uw {
namespace {

// The page's icon as a data: URL, or empty.
std::string FaviconOf(content::WebContents* contents) {
  const favicon::FaviconDriver* icons =
      favicon::ContentFaviconDriver::FromWebContents(contents);
  const gfx::Image favicon = icons ? icons->GetFavicon() : gfx::Image();
  if (favicon.IsEmpty()) {
    return std::string();
  }
  const std::optional<std::vector<uint8_t>> png =
      gfx::PNGCodec::EncodeBGRASkBitmap(*favicon.ToSkBitmap(),
                                        /*discard_transparency=*/false);
  return png ? base::StrCat(
                   {"data:image/png;base64,", base::Base64Encode(*png)})
             : std::string();
}

}  // namespace

GURL WatchCardUrl(const tabs::TabInterface& tab) {
  return GURL(base::StrCat(
      {kWatchCardUrl,
       "?tab=", base::NumberToString(tab.GetHandle().raw_value())}));
}

UwWatchCardHandler::UwWatchCardHandler(
    UwWatchCardUI* ui,
    mojo::PendingRemote<mojom::WatchCardPage> page,
    mojo::PendingReceiver<mojom::WatchCardHandler> receiver)
    : ui_(ui), page_(std::move(page)), receiver_(this, std::move(receiver)) {
  content::WebContents* contents = ui_->web_ui()->GetWebContents();
  std::string tab;
  int handle = 0;
  if (net::GetValueForKeyInQuery(contents->GetLastCommittedURL(), "tab",
                                 &tab) &&
      base::StringToInt(tab, &handle)) {
    tab_ = tabs::TabHandle(handle);
  }
  if (ScheduledChats* chats = ScheduledChats::For(
          Profile::FromBrowserContext(contents->GetBrowserContext()))) {
    observation_.Observe(chats);
  }
  LoadRuns();
}

UwWatchCardHandler::~UwWatchCardHandler() = default;

void UwWatchCardHandler::ShowUI() {
  if (auto embedder = ui_->embedder()) {
    embedder->ShowUI();
  }
}

void UwWatchCardHandler::Watch(mojom::ChatSchedulePtr schedule,
                               const std::string& watch_for) {
  ScheduledChats* chats = observation_.GetSource();
  tabs::TabInterface* tab = Tab();
  std::optional<ScheduledChat> chat = ScheduledChatFrom(*schedule);
  if (!chats || !tab || !chat) {
    return;
  }
  const GURL& page = tab->GetContents()->GetLastCommittedURL();
  if (!page.SchemeIsHTTPOrHTTPS()) {
    return;
  }
  chat->page = WatchedAddress(page).spec();
  chat->watch_for = watch_for;
  if (const std::string id = Watcher(); !id.empty()) {
    chats->Save(id, std::move(*chat));
  } else {
    chats->Watch(std::move(*chat));
  }
  Close();
}

void UwWatchCardHandler::StopWatching() {
  if (const std::string id = Watcher(); !id.empty()) {
    observation_.GetSource()->Remove(id);
  }
  Close();
}

void UwWatchCardHandler::OpenChat() {
  const std::string id = Watcher();
  tabs::TabInterface* tab = Tab();
  if (!id.empty() && tab) {
    const std::vector<tabs::TabInterface*> showing =
        TabsShowingChat(Profile::FromWebUI(ui_->web_ui()), id);
    if (showing.empty()) {
      chrome::AddTabAt(tab->GetBrowserWindowInterface(), ChatUrl(id), -1, true);
    } else {
      GoToTab(showing[0]);
    }
  }
  Close();
}

void UwWatchCardHandler::Close() {
  if (auto embedder = ui_->embedder()) {
    embedder->CloseUI();
  }
}

void UwWatchCardHandler::OnScheduleChanged(const std::string& chat_id) {
  LoadRuns();
}

tabs::TabInterface* UwWatchCardHandler::Tab() const {
  return tab_.Get();
}

std::string UwWatchCardHandler::Watcher() const {
  const ScheduledChats* chats = observation_.GetSource();
  const tabs::TabInterface* tab = Tab();
  const std::string* id =
      chats && tab
          ? chats->ChatWatching(tab->GetContents()->GetLastCommittedURL())
          : nullptr;
  return id ? *id : std::string();
}

void UwWatchCardHandler::LoadRuns() {
  const std::string id = Watcher();
  if (id.empty()) {
    runs_.clear();
    SendCard();
    return;
  }
  // A run's lines go into the chat file on its sequence before the schedule
  // says it changed, so this reads them.
  ChatFile(Profile::FromWebUI(ui_->web_ui())->GetPath(), id)
      .Load(base::BindOnce(&UwWatchCardHandler::OnRunsLoaded,
                           weak_factory_.GetWeakPtr(), id));
}

void UwWatchCardHandler::OnRunsLoaded(const std::string& chat_id,
                                      std::optional<base::DictValue> saved) {
  if (chat_id != Watcher()) {
    return;
  }
  constexpr base::TimeDelta kChart = base::Days(14);
  const base::Time since = base::Time::Now() - kChart;
  runs_.clear();
  const base::ListValue* log = saved ? saved->FindList("log") : nullptr;
  if (log) {
    for (const base::Value& entry : *log) {
      const base::DictValue* line = entry.GetIfDict();
      const std::string* kind = line ? line->FindString("kind") : nullptr;
      if (kind && *kind == "run" &&
          base::ValueToTime(line->Find("time")).value_or(base::Time()) >=
              since) {
        runs_.Append(line->Clone());
      }
    }
  }
  SendCard();
}

void UwWatchCardHandler::SendCard() {
  tabs::TabInterface* tab = Tab();
  if (!tab) {
    return;
  }
  content::WebContents* contents = tab->GetContents();
  auto card = mojom::WatchCardState::New();
  card->title = base::UTF16ToUTF8(contents->GetTitle());
  card->host = url_formatter::StripWWW(
      std::string(contents->GetLastCommittedURL().host()));
  card->favicon = FaviconOf(contents);
  card->frosted = ElementGlassOfWindow(tab->GetBrowserWindowInterface()) > 0;
  const ScheduledChats* chats = observation_.GetSource();
  if (const std::string id = Watcher(); !id.empty()) {
    const ScheduledChat& chat = *chats->Find(id);
    card->chat_id = id;
    card->watch_for = chat.watch_for;
    card->state = ScheduleStateOf(chats, id, chat);
    for (const base::Value& entry : runs_) {
      auto run = mojom::Step::New();
      run->kind = mojom::StepKind::kRun;
      if (const std::string* text = entry.GetDict().FindString("text")) {
        run->text = *text;
      }
      run->run = RunLineFrom(entry.GetDict());
      card->runs.push_back(std::move(run));
    }
  } else {
    const PrefService& prefs =
        *Profile::FromBrowserContext(contents->GetBrowserContext())->GetPrefs();
    card->state = ScheduleStateOf(
        chats, {},
        DefaultSchedule(chats, AgentChoice(prefs),
                        ModeNamed(prefs.GetString(kAgentApprovalModePref))));
  }
  page_->OnCard(std::move(card));
}

}  // namespace uw
