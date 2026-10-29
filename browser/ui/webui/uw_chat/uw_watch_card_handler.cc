// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/browser/ui/webui/uw_chat/uw_watch_card_handler.h"

#include <algorithm>
#include <optional>
#include <utility>
#include <vector>

#include "base/base64.h"
#include "base/functional/bind.h"
#include "base/json/values_util.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/favicon/favicon_service_factory.h"
#include "chrome/browser/history/history_service_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/tab_list/tab_list_interface.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface_iterator.h"
#include "components/favicon/content/content_favicon_driver.h"
#include "components/favicon/core/favicon_service.h"
#include "components/favicon_base/favicon_types.h"
#include "components/history/core/browser/history_service.h"
#include "components/keyed_service/core/service_access_type.h"
#include "components/prefs/pref_service.h"
#include "components/url_formatter/url_formatter.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_ui.h"
#include "net/base/url_util.h"
#include "ui/gfx/codec/png_codec.h"
#include "ui/gfx/favicon_size.h"
#include "ui/gfx/image/image.h"
#include "uw/browser/agent/chat_file.h"
#include "uw/browser/agent/chat_tab.h"
#include "uw/browser/agent/run_window_mark.h"
#include "uw/browser/ai_providers/models.h"
#include "uw/browser/tab_organization/ai_connection.h"
#include "uw/browser/ui/glass/glass.h"
#include "uw/browser/ui/webui/uw_chat/schedule_card.h"
#include "uw/browser/ui/webui/uw_chat/uw_watch_card_ui.h"

namespace uw {
namespace {

std::string PngDataUrl(base::span<const uint8_t> png) {
  return base::StrCat({"data:image/png;base64,", base::Base64Encode(png)});
}

}  // namespace

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
  return png ? PngDataUrl(*png) : std::string();
}

tabs::TabInterface* TabOfPage(Profile* profile, const GURL& page) {
  for (BrowserWindowInterface* window : GetAllBrowserWindowInterfaces()) {
    if (window->GetProfile() != profile || IsRunWindow(window)) {
      continue;
    }
    for (tabs::TabInterface* tab :
         TabListInterface::From(window)->GetAllTabs()) {
      const GURL& url = tab->GetContents()->GetLastCommittedURL();
      if (url.SchemeIsHTTPOrHTTPS() && WatchedAddress(url) == page) {
        return tab;
      }
    }
  }
  return nullptr;
}

void LookUpPage(Profile* profile,
                const GURL& page,
                base::CancelableTaskTracker* tracker,
                base::OnceCallback<void(const std::string& title,
                                        const std::string& favicon)> done) {
  if (tabs::TabInterface* tab = TabOfPage(profile, page)) {
    std::move(done).Run(base::UTF16ToUTF8(tab->GetContents()->GetTitle()),
                        FaviconOf(tab->GetContents()));
    return;
  }
  history::HistoryService* history = HistoryServiceFactory::GetForProfile(
      profile, ServiceAccessType::EXPLICIT_ACCESS);
  favicon::FaviconService* icons = FaviconServiceFactory::GetForProfile(
      profile, ServiceAccessType::EXPLICIT_ACCESS);
  if (!history || !icons) {
    std::move(done).Run(std::string(), std::string());
    return;
  }
  history->QueryURL(
      page,
      base::BindOnce(
          [](favicon::FaviconService* icons, GURL page,
             base::CancelableTaskTracker* tracker,
             base::OnceCallback<void(const std::string&, const std::string&)>
                 done,
             history::QueryURLResult result) {
            std::string title = result.success
                                    ? base::UTF16ToUTF8(result.row.title())
                                    : std::string();
            icons->GetRawFaviconForPageURL(
                page, {favicon_base::IconType::kFavicon}, gfx::kFaviconSize,
                /*fallback_to_host=*/true,
                base::BindOnce(
                    [](base::OnceCallback<void(const std::string&,
                                               const std::string&)> done,
                       std::string title,
                       const favicon_base::FaviconRawBitmapResult& icon) {
                      std::move(done).Run(
                          title, icon.is_valid()
                                     ? PngDataUrl(base::span<const uint8_t>(
                                           *icon.bitmap_data))
                                     : std::string());
                    },
                    std::move(done), std::move(title)),
                tracker);
          },
          base::Unretained(icons), page, base::Unretained(tracker),
          std::move(done)),
      tracker);
}

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

void UwWatchCardHandler::Watch(const std::string& chat_id,
                               mojom::ChatSchedulePtr schedule) {
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
  chat->watch_for = schedule->watch_for;
  if (chat_id.empty()) {
    chats->Watch(std::move(*chat));
  } else if (std::ranges::contains(Watchers(), chat_id)) {
    chats->Save(chat_id, std::move(*chat));
  }
}

void UwWatchCardHandler::StopWatching(const std::string& chat_id) {
  if (std::ranges::contains(Watchers(), chat_id)) {
    observation_.GetSource()->Remove(chat_id);
  }
}

void UwWatchCardHandler::CheckNow(const std::string& chat_id) {
  if (std::ranges::contains(Watchers(), chat_id)) {
    observation_.GetSource()->CheckNow(chat_id);
  }
}

void UwWatchCardHandler::OpenChat(const std::string& chat_id) {
  tabs::TabInterface* tab = Tab();
  if (tab && std::ranges::contains(Watchers(), chat_id)) {
    const std::vector<tabs::TabInterface*> showing =
        TabsShowingChat(Profile::FromWebUI(ui_->web_ui()), chat_id);
    if (showing.empty()) {
      chrome::AddTabAt(tab->GetBrowserWindowInterface(), ChatUrl(chat_id), -1,
                       true);
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

std::vector<std::string> UwWatchCardHandler::Watchers() const {
  const ScheduledChats* chats = observation_.GetSource();
  const tabs::TabInterface* tab = Tab();
  return chats && tab
             ? chats->ChatsWatching(tab->GetContents()->GetLastCommittedURL())
             : std::vector<std::string>();
}

void UwWatchCardHandler::LoadRuns() {
  const std::vector<std::string> watchers = Watchers();
  std::erase_if(runs_, [&watchers](const auto& runs) {
    return !std::ranges::contains(watchers, runs.first);
  });
  if (watchers.empty()) {
    SendCard();
  }
  // A run's lines go into the chat file on its sequence before the schedule
  // says it changed, so this reads them.
  for (const std::string& id : watchers) {
    ChatFile(Profile::FromWebUI(ui_->web_ui())->GetPath(), id)
        .Load(base::BindOnce(&UwWatchCardHandler::OnRunsLoaded,
                             weak_factory_.GetWeakPtr(), id));
  }
}

void UwWatchCardHandler::OnRunsLoaded(const std::string& chat_id,
                                      std::optional<base::DictValue> saved) {
  if (!std::ranges::contains(Watchers(), chat_id)) {
    return;
  }
  constexpr base::TimeDelta kChart = base::Days(14);
  const base::Time since = base::Time::Now() - kChart;
  base::ListValue& runs = runs_[chat_id];
  runs.clear();
  const base::ListValue* log = saved ? saved->FindList("log") : nullptr;
  if (log) {
    for (const base::Value& entry : *log) {
      const base::DictValue* line = entry.GetIfDict();
      const std::string* kind = line ? line->FindString("kind") : nullptr;
      if (kind && *kind == "run" &&
          base::ValueToTime(line->Find("time")).value_or(base::Time()) >=
              since) {
        runs.Append(line->Clone());
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
  for (const std::string& id : Watchers()) {
    auto watch = mojom::PageWatch::New();
    watch->chat_id = id;
    watch->state = ScheduleStateOf(chats, id, *chats->Find(id));
    if (const auto runs = runs_.find(id); runs != runs_.end()) {
      for (const base::Value& entry : runs->second) {
        auto run = mojom::Step::New();
        run->kind = mojom::StepKind::kRun;
        if (const std::string* text = entry.GetDict().FindString("text")) {
          run->text = *text;
        }
        run->run = RunLineFrom(entry.GetDict());
        watch->runs.push_back(std::move(run));
      }
    }
    card->watches.push_back(std::move(watch));
  }
  const PrefService& prefs =
      *Profile::FromBrowserContext(contents->GetBrowserContext())->GetPrefs();
  card->fresh = ScheduleStateOf(
      chats, {},
      DefaultSchedule(chats, AgentChoice(prefs),
                      ModeNamed(prefs.GetString(kAgentApprovalModePref))));
  page_->OnCard(std::move(card));
}

}  // namespace uw
