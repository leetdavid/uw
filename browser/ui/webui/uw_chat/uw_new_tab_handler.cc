// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/browser/ui/webui/uw_chat/uw_new_tab_handler.h"

#include <algorithm>
#include <limits>
#include <string_view>
#include <utility>
#include <vector>

#include "base/barrier_callback.h"
#include "base/containers/span.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/strings/strcat.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/bind_post_task.h"
#include "base/task/sequenced_task_runner.h"
#include "base/values.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/tab_list/tab_list_interface.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface_iterator.h"
#include "chrome/browser/ui/singleton_tabs.h"  // nogncheck
#include "chrome/browser/ui/webui/webui_embedding_context.h"
#include "components/prefs/pref_service.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/media_session.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_ui.h"
#include "mojo/public/cpp/bindings/callback_helpers.h"
#include "ui/base/page_transition_types.h"
#include "uw/browser/agent/chat_file.h"
#include "uw/browser/agent/run_window_mark.h"
#include "uw/browser/ai_providers/api_key_store.h"
#include "uw/browser/ai_providers/models.h"
#include "uw/browser/ai_providers/rejected_keys.h"
#include "uw/browser/tab_organization/ai_connection.h"
#include "uw/browser/tab_organization/automatic_organizer.h"
#include "uw/browser/tab_organization/commands.h"
#include "uw/browser/tab_organization/tab_organization_controller.h"
#include "uw/browser/terminal/terminal_shell.h"
#include "uw/browser/ui/webui/uw_chat/schedule_card.h"
#include "uw/browser/ui/webui/uw_chat/uw_chat_handler.h"
#include "uw/browser/ui/webui/uw_chat/uw_scheduled_ui.h"
#include "uw/browser/ui/webui/uw_chat/uw_watch_card_handler.h"
#include "uw/browser/update/pending_update.h"
#include "uw/components/ai_providers/providers.h"

namespace uw {
namespace {

constexpr size_t kFinishedRows = 5;
constexpr size_t kComingUpRows = 3;
constexpr size_t kTabsRows = 3;
constexpr base::TimeDelta kPollEvery = base::Seconds(2);

// How the chat file at `path` ended, on the chats' sequence.
UwNewTabHandler::Ending ReadEnding(const base::FilePath& path,
                                   UwNewTabHandler::Ending ending) {
  std::string json;
  base::File::Info info;
  if (!base::ReadFileToString(path, &json) || !base::GetFileInfo(path, &info)) {
    return ending;
  }
  std::optional<base::DictValue> chat =
      base::JSONReader::ReadDict(json, base::JSON_PARSE_RFC);
  const base::ListValue* log = chat ? chat->FindList("log") : nullptr;
  if (!log) {
    return ending;
  }
  for (auto line = log->rbegin(); line != log->rend(); ++line) {
    const base::DictValue* dict = line->GetIfDict();
    const std::string* kind = dict ? dict->FindString("kind") : nullptr;
    const std::string* text = dict ? dict->FindString("text") : nullptr;
    if (kind && text && (*kind == "answer" || *kind == "failed")) {
      ending.what = *kind == "answer" ? "Answered" : "Didn't finish";
      ending.text = *text;
      ending.time = info.last_modified;
      break;
    }
  }
  return ending;
}

}  // namespace

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
    prefs_.Add(pref, base::BindRepeating(&UwNewTabHandler::OnModelsChanged,
                                         base::Unretained(this)));
  }
  SendBox();
  // A private profile keeps no chats, so its page has no sections.
  ChatAlerts* alerts = ChatAlerts::For(profile());
  ChatIndex* index = ChatIndex::For(profile());
  ScheduledChats* chats = ScheduledChats::For(profile());
  if (alerts && index && chats) {
    alerts_.Observe(alerts);
    index_.Observe(index);
    schedules_.Observe(chats);
    pictures_.Observe(PictureInPictureWindowManager::GetInstance());
    WatchVideo();
    if (BrowserWindowInterface* window = Window()) {
      if (AutomaticOrganizer* organizer = AutomaticOrganizer::From(window)) {
        organizer_events_ = organizer->ObserveEvents(base::BindRepeating(
            [](UwNewTabHandler* self, const AutomaticOrganizer::Event&) {
              self->SendSections();
            },
            base::Unretained(this)));
      }
      if (TabOrganizationController* controller =
              TabOrganizationController::From(window)) {
        tree_changes_ = controller->Observe(base::BindRepeating(
            &UwNewTabHandler::SendSections, base::Unretained(this)));
      }
    }
    for (const char* pref :
         {kOrganizationEnabledPref, kOrganizationPauseModePref,
          kOrganizationPauseUntilPref}) {
      prefs_.Add(pref, base::BindRepeating(&UwNewTabHandler::SendSections,
                                           base::Unretained(this)));
    }
    connectors_.Observe(&Connectors::Get());
    rejected_keys_ = RejectedKeys::Get().Observe(base::BindRepeating(
        [](UwNewTabHandler* self) {
          self->CheckKeys();
          self->SendSections();
        },
        base::Unretained(this)));
    CheckKeys();
    SendSections();
    poll_.Start(
        FROM_HERE, kPollEvery,
        base::BindRepeating(&UwNewTabHandler::Poll, base::Unretained(this)));
    Poll();
  }
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

void UwNewTabHandler::OpenChat(const std::string& chat_id,
                               const std::string& alert_id) {
  Remove(chat_id, alert_id);
  const std::vector<tabs::TabInterface*> showing =
      TabsShowingChat(profile(), chat_id);
  if (!showing.empty()) {
    GoToTab(showing[0]);
  } else if (ChatFile::IsChatId(chat_id)) {
    Become(ChatUrl(chat_id), std::nullopt);
  }
}

void UwNewTabHandler::OpenPage(const std::string& chat_id,
                               const std::string& alert_id) {
  const ScheduledChats* chats = schedules_.GetSource();
  const ScheduledChat* chat = chats ? chats->Find(chat_id) : nullptr;
  if (!chat || chat->page.empty()) {
    OpenChat(chat_id, alert_id);
    return;
  }
  Remove(chat_id, alert_id);
  const GURL page(chat->page);
  if (tabs::TabInterface* tab = TabOfPage(profile(), page)) {
    GoToTab(tab);
  } else {
    Become(page, std::nullopt);
  }
}

void UwNewTabHandler::Dismiss(const std::string& chat_id,
                              const std::string& alert_id) {
  Remove(chat_id, alert_id);
}

void UwNewTabHandler::ShowInbox() {
  if (BrowserWindowInterface* window =
          webui::GetBrowserWindowInterface(web_ui_->GetWebContents())) {
    chrome::ExecuteCommand(window, kShowInbox);
  }
}

void UwNewTabHandler::ShowScheduled() {
  if (BrowserWindowInterface* window =
          webui::GetBrowserWindowInterface(web_ui_->GetWebContents())) {
    ShowSingletonTabOverwritingNTP(window, GURL(kScheduledUrl));
  }
}

void UwNewTabHandler::ShowTab(int32_t tab) {
  tabs::TabInterface* shown = tabs::TabHandle(tab).Get();
  if (shown && shown->GetContents()->GetBrowserContext() == profile()) {
    GoToTab(shown);
  }
}

void UwNewTabHandler::PlayOrPause(int32_t tab) {
  content::WebContents* contents = video_.get();
  tabs::TabInterface* shown =
      contents ? tabs::TabInterface::MaybeGetFromContents(contents) : nullptr;
  if (!shown || shown->GetHandle().raw_value() != tab) {
    return;
  }
  content::MediaSession* session = content::MediaSession::Get(contents);
  if (playing_) {
    session->Suspend(content::MediaSession::SuspendType::kUI);
  } else {
    session->Resume(content::MediaSession::SuspendType::kUI);
  }
}

void UwNewTabHandler::UndoChange() {
  BrowserWindowInterface* window = Window();
  AutomaticOrganizer* organizer =
      window ? AutomaticOrganizer::From(window) : nullptr;
  TabOrganizationController* controller =
      window ? TabOrganizationController::From(window) : nullptr;
  if (!organizer || !controller) {
    return;
  }
  const std::vector<AutomaticOrganizer::Change> changes =
      organizer->RecentChanges();
  const std::optional<TabOrganizationController::UndoMove> next =
      controller->NextUndo();
  if (!changes.empty() && next && next->step == changes.front().step) {
    controller->Undo();
  }
}

void UwNewTabHandler::Pause(mojom::OrganizationPause pause) {
  SetPauseState(profile()->GetPrefs(), static_cast<PauseMode>(pause),
                pause == mojom::OrganizationPause::kTimed ? base::Hours(1)
                                                          : base::TimeDelta());
}

void UwNewTabHandler::Relaunch() {
  InstallPendingUpdate();
}

void UwNewTabHandler::OnConnectorsChanged() {
  SendSections();
}

void UwNewTabHandler::OnAlertsChanged() {
  SendSections();
}

void UwNewTabHandler::OnChatsChanged() {
  SendSections();
}

void UwNewTabHandler::OnScheduleChanged(const std::string& chat_id) {
  SendSections();
}

void UwNewTabHandler::OnEnterPictureInPicture() {
  WatchVideo();
  SendSections();
}

void UwNewTabHandler::OnExitPictureInPicture() {
  WatchVideo();
  SendSections();
}

void UwNewTabHandler::MediaSessionInfoChanged(
    media_session::mojom::MediaSessionInfoPtr info) {
  playing_ = info && info->playback_state ==
                         media_session::mojom::MediaPlaybackState::kPlaying;
  SendSections();
}

void UwNewTabHandler::MediaSessionMetadataChanged(
    const std::optional<media_session::MediaMetadata>& metadata) {
  video_title_ = metadata ? metadata->title : std::u16string();
  SendSections();
}

void UwNewTabHandler::MediaSessionPositionChanged(
    const std::optional<media_session::MediaPosition>& position) {
  position_ = position;
  SendSections();
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

void UwNewTabHandler::SendSections() {
  const ChatAlerts* alerts = alerts_.GetSource();
  const ChatIndex* index = index_.GetSource();
  const ScheduledChats* chats = schedules_.GetSource();
  if (!alerts || !index || !chats) {
    return;
  }
  const std::vector<ChatIndex::Entry> entries = index->Entries();
  const auto entry_of = [&entries](const std::string& id) {
    const auto entry = std::ranges::find(entries, id, &ChatIndex::Entry::id);
    return entry == entries.end() ? nullptr : &*entry;
  };
  auto sections = mojom::NewTabSections::New();

  for (const ChatIndex::Entry& entry : entries) {
    mojom::NewTabRowPtr row;
    if (chats->Waiting(entry.id)) {
      row = RowOf(entry);
      row->what = "Waits for your approval";
      const AgentTools::Approval* step = chats->WaitingStep(entry.id);
      row->text = step ? step->action : std::string();
      const auto alert = std::ranges::find_if(
          alerts->alerts(), [&entry](const ChatAlerts::Alert& alert) {
            return alert.chat_id == entry.id && alert.waiting;
          });
      row->time =
          (alert != alerts->alerts().end() ? alert->time : entry.updated)
              .InMillisecondsFSinceUnixEpoch();
    } else if (entry.state == ChatIndex::State::kWaiting) {
      row = RowOf(entry);
      const UwChatHandler* chat = UwChatHandler::ForChat(entry.id);
      const mojom::Approval* step = chat ? chat->waiting() : nullptr;
      // Else it waits for a password in one of its terminal tabs.
      row->what = step ? "Waits for your approval" : "Waits for your password";
      row->text = step ? step->action : std::string();
      row->time = entry.updated.InMillisecondsFSinceUnixEpoch();
    } else {
      continue;
    }
    sections->needs_you.push_back(std::move(row));
  }

  std::vector<mojom::NewTabRowPtr> finished;
  for (const ChatAlerts::Alert& alert : alerts->alerts()) {
    const ChatIndex::Entry* entry = entry_of(alert.chat_id);
    if (alert.seen || alert.waiting || alert.what == "No change" || !entry) {
      continue;
    }
    mojom::NewTabRowPtr row = RowOf(*entry);
    row->alert_id = alert.id;
    row->what = alert.what;
    row->text = alert.text;
    row->time = alert.time.InMillisecondsFSinceUnixEpoch();
    // A run that could not sign in waits for the user to.
    (alert.what == "Sign in required" ? sections->needs_you : finished)
        .push_back(std::move(row));
  }
  for (const ChatIndex::Entry& entry : entries) {
    if (entry.state != ChatIndex::State::kUnread ||
        std::ranges::any_of(finished, [&entry](const auto& row) {
          return row->chat_id == entry.id;
        })) {
      continue;
    }
    Ending& ending = endings_[entry.id];
    if (ending.updated != entry.updated) {
      ending = {.updated = entry.updated};
      ChatFile::Sequence()->PostTaskAndReplyWithResult(
          FROM_HERE,
          base::BindOnce(&ReadEnding,
                         ChatFile::FolderFor(profile()->GetPath())
                             .AppendASCII(base::StrCat({entry.id, ".json"})),
                         ending),
          base::BindOnce(&UwNewTabHandler::OnRead, weak_factory_.GetWeakPtr(),
                         entry.id));
    }
    if (ending.time.is_null()) {
      continue;
    }
    mojom::NewTabRowPtr row = RowOf(entry);
    row->what = ending.what;
    row->text = ending.text;
    row->time = ending.time.InMillisecondsFSinceUnixEpoch();
    finished.push_back(std::move(row));
  }
  std::ranges::sort(sections->needs_you, std::ranges::less(),
                    [](const auto& row) { return row->time; });
  std::ranges::sort(finished, std::ranges::greater(),
                    [](const auto& row) { return row->time; });
  sections->finished_count = static_cast<int32_t>(finished.size());
  finished.resize(std::min(finished.size(), kFinishedRows));
  sections->finished = std::move(finished);

  std::vector<mojom::NewTabRowPtr> coming_up;
  for (const ChatIndex::Entry& entry : entries) {
    const ScheduledChat* chat = chats->Find(entry.id);
    const std::optional<base::Time> next = chats->NextRunOf(entry.id);
    const ChatTriggers::Trigger* trigger = chats->TriggerOf(entry.id);
    if ((chat && chat->paused) || (!next && !trigger)) {
      continue;
    }
    mojom::NewTabRowPtr row = RowOf(entry);
    if (next && chat) {
      row->time = next->InMillisecondsFSinceUnixEpoch();
      row->schedule =
          std::move(ScheduleStateOf(chats, entry.id, *chat)->schedule);
    }
    coming_up.push_back(std::move(row));
  }
  // Runs a trigger starts come after the timed ones.
  std::ranges::sort(coming_up, std::ranges::less(), [](const auto& row) {
    return row->time ? row->time : std::numeric_limits<double>::max();
  });
  sections->coming_up_count = static_cast<int32_t>(coming_up.size());
  coming_up.resize(std::min(coming_up.size(), kComingUpRows));
  sections->coming_up = std::move(coming_up);

  for (const ChatIndex::Entry& entry : entries) {
    if (entry.state != ChatIndex::State::kRunning &&
        !(chats->Running(entry.id) && !chats->Waiting(entry.id))) {
      continue;
    }
    auto row = mojom::NewTabRunningRow::New();
    row->chat_id = entry.id;
    row->title = entry.title;
    row->scheduled = chats->Find(entry.id) != nullptr;
    sections->running.push_back(std::move(row));
  }
  for (const auto& [handle, program] : busy_) {
    if (tabs::TabInterface* tab = tabs::TabHandle(handle).Get()) {
      auto row = mojom::NewTabRunningRow::New();
      row->tab = handle;
      row->title = base::UTF16ToUTF8(tab->GetContents()->GetTitle());
      row->program = program;
      sections->running.push_back(std::move(row));
    }
  }
  content::WebContents* video = video_.get();
  if (tabs::TabInterface* tab =
          video ? tabs::TabInterface::MaybeGetFromContents(video) : nullptr) {
    auto row = mojom::NewTabRunningRow::New();
    row->tab = tab->GetHandle().raw_value();
    row->title = base::UTF16ToUTF8(video_title_.empty() ? video->GetTitle()
                                                        : video_title_);
    row->favicon = FaviconOf(video);
    row->video = true;
    row->playing = playing_;
    if (position_) {
      row->position = position_->GetPosition().InSecondsF();
      row->duration = position_->duration().is_inf()
                          ? 0
                          : position_->duration().InSecondsF();
      row->position_time = base::Time::Now().InMillisecondsFSinceUnixEpoch();
    }
    sections->running.push_back(std::move(row));
  }

  if (BrowserWindowInterface* window = Window()) {
    sections->tabs = TabsOf(window);
  }

  for (const Provider& provider : rejected_) {
    sections->setup.push_back(mojom::NewTabSetupRow::New(
        mojom::NewTabSetupKind::kKeyRejected, provider.id, provider.name,
        static_cast<int32_t>(provider.preset)));
  }
  for (const Connectors::Info& connector : Connectors::Get().list()) {
    if (connector.on && connector.state == Connectors::State::kNeedsSignIn) {
      sections->setup.push_back(mojom::NewTabSetupRow::New(
          mojom::NewTabSetupKind::kSignIn, connector.config.name,
          connector.config.name, 0));
    }
  }
  if (HasPendingUpdate()) {
    sections->setup.push_back(mojom::NewTabSetupRow::New(
        mojom::NewTabSetupKind::kUpdate, std::string(), std::string(), 0));
  }
  if (ReadProviders(*profile()->GetPrefs()).empty()) {
    sections->setup.push_back(mojom::NewTabSetupRow::New(
        mojom::NewTabSetupKind::kNoProvider, std::string(), std::string(), 0));
  }

  page_->OnSections(std::move(sections));
}

mojom::NewTabRowPtr UwNewTabHandler::RowOf(const ChatIndex::Entry& entry) {
  const ScheduledChats* chats = schedules_.GetSource();
  const ScheduledChat* chat = chats->Find(entry.id);
  const ChatTriggers::Trigger* trigger = chats->TriggerOf(entry.id);
  auto row = mojom::NewTabRow::New();
  row->chat_id = entry.id;
  row->title = entry.title;
  row->scheduled = chat != nullptr;
  row->trigger = trigger ? trigger->command : std::string();
  if (chat && !chat->page.empty()) {
    row->page = chat->page;
    // The page's address until uw knows its title, as on the Scheduled page.
    const std::string_view address = chat->page;
    row->title = std::string(
        base::RemovePrefix(address, "https://")
            .value_or(
                base::RemovePrefix(address, "http://").value_or(address)));
    if (const auto page = pages_.find(chat->page); page != pages_.end()) {
      if (!page->second.first.empty()) {
        row->title = page->second.first;
      }
      row->favicon = page->second.second;
    } else {
      // Kept empty until the page is looked up, so that it is looked up once.
      // An open tab answers at once, so the answer waits for this send to end.
      pages_.try_emplace(chat->page);
      LookUpPage(profile(), GURL(chat->page), &tracker_,
                 base::BindPostTaskToCurrentDefault(
                     base::BindOnce(&UwNewTabHandler::OnLooked,
                                    weak_factory_.GetWeakPtr(), chat->page)));
    }
  }
  return row;
}

void UwNewTabHandler::OnLooked(const std::string& page,
                               const std::string& title,
                               const std::string& favicon) {
  pages_[page] = {title, favicon};
  SendSections();
}

void UwNewTabHandler::OnRead(const std::string& chat_id, Ending ending) {
  // A newer read is under way.
  if (endings_[chat_id].updated != ending.updated) {
    return;
  }
  endings_[chat_id] = std::move(ending);
  SendSections();
}

void UwNewTabHandler::Remove(const std::string& chat_id,
                             const std::string& alert_id) {
  if (!alert_id.empty()) {
    if (ChatAlerts* alerts = alerts_.GetSource()) {
      alerts->See(alert_id);
    }
  } else if (ChatIndex* index = index_.GetSource()) {
    index->MarkRead(chat_id);
  }
}

BrowserWindowInterface* UwNewTabHandler::Window() const {
  return webui::GetBrowserWindowInterface(web_ui_->GetWebContents());
}

mojom::NewTabTabsPtr UwNewTabHandler::TabsOf(
    BrowserWindowInterface* window) const {
  const PrefService& prefs = *profile()->GetPrefs();
  const AutomaticOrganizer* organizer = AutomaticOrganizer::From(window);
  const TabOrganizationController* controller =
      TabOrganizationController::From(window);
  if (!organizer || !controller ||
      !prefs.GetBoolean(kOrganizationEnabledPref)) {
    return nullptr;
  }
  auto tabs = mojom::NewTabTabs::New();
  const std::vector<AutomaticOrganizer::Change> changes =
      organizer->RecentChanges();
  const std::optional<TabOrganizationController::UndoMove> next =
      controller->NextUndo();
  tabs->can_undo = !changes.empty() && next && changes.front().step &&
                   next->step == changes.front().step;
  for (const AutomaticOrganizer::Change& change :
       base::span(changes).first(std::min(changes.size(), kTabsRows))) {
    tabs->changes.push_back(mojom::NewTabChange::New(
        change.event.kind == AutomaticOrganizer::Event::Kind::kCreated,
        base::UTF16ToUTF8(change.event.page),
        base::UTF16ToUTF8(change.event.detail),
        change.time.InMillisecondsFSinceUnixEpoch()));
  }
  const PauseMode pause = ReadPauseState(&prefs);
  tabs->pause = static_cast<mojom::OrganizationPause>(pause);
  if (pause == PauseMode::kTimed) {
    tabs->paused_until = PausedUntil(&prefs).InMillisecondsFSinceUnixEpoch();
  }
  return tabs;
}

void UwNewTabHandler::CheckKeys() {
  rejected_.clear();
  if (RejectedKeys::Get().empty()) {
    return;
  }
  ApiKeyStore keys;
  for (Provider& provider : ReadProviders(*profile()->GetPrefs())) {
    const std::optional<std::string> key =
        provider.key_account.empty() ? std::nullopt
                                     : keys.GetKey(provider.key_account);
    if (key && RejectedKeys::Get().Rejected(*key)) {
      rejected_.push_back(std::move(provider));
    }
  }
}

void UwNewTabHandler::OnModelsChanged() {
  SendBox();
  CheckKeys();
  SendSections();
}

void UwNewTabHandler::Poll() {
  if (web_ui_->GetWebContents()->GetVisibility() ==
      content::Visibility::HIDDEN) {
    return;
  }
  std::vector<tabs::TabInterface*> terminals;
  for (BrowserWindowInterface* window : GetAllBrowserWindowInterfaces()) {
    if (window->GetProfile() != profile() || IsRunWindow(window)) {
      continue;
    }
    for (tabs::TabInterface* tab :
         TabListInterface::From(window)->GetAllTabs()) {
      if (TerminalShell::From(tab->GetContents())) {
        terminals.push_back(tab);
      }
    }
  }
  const auto answered = base::BarrierCallback<std::pair<int32_t, std::string>>(
      terminals.size(),
      base::BindOnce(&UwNewTabHandler::OnPolled, weak_factory_.GetWeakPtr()));
  // A terminal that closes before its shell answers counts as idle.
  for (tabs::TabInterface* tab : terminals) {
    TerminalShell::From(tab->GetContents())
        ->GetActivity(mojo::WrapCallbackWithDefaultInvokeIfNotRun(
            base::BindOnce(
                [](base::RepeatingCallback<void(
                       std::pair<int32_t, std::string>)> answered,
                   int32_t handle, ShellHost::Activity activity) {
                  answered.Run({handle, activity.program});
                },
                answered, tab->GetHandle().raw_value()),
            ShellHost::Activity()));
  }
}

void UwNewTabHandler::OnPolled(
    std::vector<std::pair<int32_t, std::string>> programs) {
  std::map<int32_t, std::string> busy;
  for (auto& [handle, program] : programs) {
    if (!program.empty()) {
      busy.emplace(handle, std::move(program));
    }
  }
  if (busy != busy_) {
    busy_ = std::move(busy);
    SendSections();
  }
}

void UwNewTabHandler::WatchVideo() {
  content::WebContents* contents =
      PictureInPictureWindowManager::GetInstance()->GetWebContents();
  if (contents && contents->GetBrowserContext() != profile()) {
    contents = nullptr;
  }
  if (contents == video_.get() && video_session_.is_bound() == !!contents) {
    return;
  }
  video_session_.reset();
  playing_ = false;
  video_title_.clear();
  position_.reset();
  video_ = contents ? contents->GetWeakPtr() : nullptr;
  if (contents) {
    content::MediaSession::Get(contents)->AddObserver(
        video_session_.BindNewPipeAndPassRemote());
  }
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
