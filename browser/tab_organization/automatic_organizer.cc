// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/browser/tab_organization/automatic_organizer.h"

#include <algorithm>
#include <functional>
#include <utility>

#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/location.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/sequenced_task_runner.h"
#include "base/time/time.h"
#include "base/values.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/grit/generated_resources.h"
#include "components/content_extraction/content/browser/inner_text.h"
#include "components/prefs/pref_service.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/storage_partition.h"
#include "content/public/browser/web_contents.h"
#include "net/base/net_errors.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/page_transition_types.h"
#include "url/origin.h"
#include "uw/browser/ai_providers/api_key_store.h"
#include "uw/browser/ai_providers/models.h"
#include "uw/browser/tab_organization/ai_connection.h"
#include "uw/browser/tab_organization/tab_organization_controller.h"
#include "uw/browser/terminal/terminal_tab.h"

namespace uw {
namespace {

constexpr base::TimeDelta kDebounce = base::Milliseconds(750);

// The key in `account`: empty when there is no account, nothing when the key
// is gone.
std::optional<std::string> KeyIn(const std::string& account) {
  if (account.empty()) {
    return std::string();
  }
  std::optional<std::string> stored = ApiKeyStore().GetKey(account);
  if (!stored || stored->empty()) {
    return std::nullopt;
  }
  return stored;
}

using Kind = AutomaticOrganizer::Event::Kind;

// A folder as a shell prompt shows it, with the home folder as ~.
std::string ShortFolder(const base::FilePath& folder) {
  const base::FilePath home = base::GetHomeDir();
  if (folder == home) {
    return "~";
  }
  base::FilePath rest;
  if (home.AppendRelativePath(folder, &rest)) {
    return "~/" + rest.AsUTF8Unsafe();
  }
  return folder.AsUTF8Unsafe();
}

// The word the organizer log uses for how a request ended.
const char* ResultName(Kind kind) {
  switch (kind) {
    case Kind::kMoved:
      return "moved";
    case Kind::kCreated:
      return "created";
    case Kind::kLeft:
      return "left";
    case Kind::kNeedsName:
      return "name";
    case Kind::kUnsure:
      return "unsure";
    case Kind::kRequestFailed:
      return "failed";
    case Kind::kAnswerUnreadable:
      return "unreadable";
    case Kind::kDropped:
      return "dropped";
    case Kind::kNameFailed:
      return "name_failed";
    case Kind::kNaming:
      return "new_task";
    // Tidy writes its own results.
    case Kind::kAsking:
    case Kind::kTidying:
    case Kind::kTidied:
    case Kind::kTidyNothing:
    case Kind::kTidyFailed:
      return "";
  }
}

// The HTTP status or the network error of a request that failed.
std::u16string FailureReason(const RemoteModelClient::Outcome& outcome) {
  return base::UTF8ToUTF16(outcome.net_error == net::OK
                               ? "HTTP " +
                                     base::NumberToString(outcome.http_status)
                               : net::ErrorToShortString(outcome.net_error));
}

// True while every page the tab has shown came from following a link, so
// what it shows now still descends from the tab that opened it. Typing an
// address, searching, or picking a bookmark breaks that: a blank new tab and
// a tab the user reused for something else were not opened from anything.
bool FollowedLinksOnly(content::WebContents* contents) {
  content::NavigationController& entries = contents->GetController();
  for (int i = 0; i <= entries.GetLastCommittedEntryIndex(); ++i) {
    const ui::PageTransition type =
        entries.GetEntryAtIndex(i)->GetTransitionType();
    if (!ui::PageTransitionCoreTypeIs(type, ui::PAGE_TRANSITION_LINK) &&
        !ui::PageTransitionCoreTypeIs(type, ui::PAGE_TRANSITION_FORM_SUBMIT) &&
        !ui::PageTransitionCoreTypeIs(type, ui::PAGE_TRANSITION_RELOAD)) {
      return false;
    }
  }
  return true;
}

// True when the user went to the page themselves: typed an address, searched
// from the address bar, or picked a bookmark. Going back or forward is not.
bool StartedByUser(const content::NavigationEntry& entry) {
  const ui::PageTransition type = entry.GetTransitionType();
  if (type & ui::PAGE_TRANSITION_FORWARD_BACK) {
    return false;
  }
  constexpr ui::PageTransition kByUser[] = {
      ui::PAGE_TRANSITION_TYPED, ui::PAGE_TRANSITION_GENERATED,
      ui::PAGE_TRANSITION_AUTO_BOOKMARK, ui::PAGE_TRANSITION_KEYWORD};
  return std::ranges::any_of(kByUser, [type](ui::PageTransition core) {
    return ui::PageTransitionCoreTypeIs(type, core);
  });
}

}  // namespace

DEFINE_USER_DATA(AutomaticOrganizer);

AutomaticOrganizer::AutomaticOrganizer(BrowserWindowInterface* window,
                                       TabOrganizationController* controller)
    : window_(window),
      controller_(controller),
      tabs_(window->GetTabStripModel()),
      user_data_(window->GetUnownedUserDataHost(), *this) {
  // An off-the-record profile writes its prefs to memory only, so the move
  // waits for a normal window.
  if (!profile()->IsOffTheRecord()) {
    MoveConnectionsToProviders(profile()->GetPrefs());
  }
  prefs_.Init(profile()->GetPrefs());
  for (const char* pref : {kOrganizationEnabledPref, kOrganizationPauseModePref,
                           kOrganizationPauseUntilPref, kProvidersPref,
                           kLanguageChoicePref, kDecisionChoicePref}) {
    prefs_.Add(pref, base::BindRepeating(&AutomaticOrganizer::OnConfigChanged,
                                         base::Unretained(this)));
  }
  tree_changed_ = controller_->Observe(
      base::BindRepeating(&AutomaticOrganizer::Scan, base::Unretained(this)));
  manual_moves_ = controller_->ObserveManualMoves(base::BindRepeating(
      &AutomaticOrganizer::OnManualMove, base::Unretained(this)));
  tabs_->AddObserver(this);
  Scan();
}

AutomaticOrganizer::~AutomaticOrganizer() {
  tabs_->RemoveObserver(this);
}

AutomaticOrganizer* AutomaticOrganizer::From(BrowserWindowInterface* window) {
  return window ? Get(window->GetUnownedUserDataHost()) : nullptr;
}

base::CallbackListSubscription AutomaticOrganizer::ObserveEvents(
    EventCallback callback) {
  return events_.Add(std::move(callback));
}

size_t AutomaticOrganizer::pending_requests() const {
  return static_cast<size_t>(
      std::ranges::count_if(
          pages_, [](const auto& entry) { return entry.second.asking; }) +
      std::ranges::count_if(requests_,
                            [](const auto& entry) {
                              return !entry.second.request.naming.body.empty();
                            }) +
      asked_names_.size());
}

bool AutomaticOrganizer::IsNaming(const std::string& task) const {
  return naming_.contains(task);
}

base::CallbackListSubscription AutomaticOrganizer::ObserveNaming(
    base::RepeatingClosure callback) {
  return naming_changed_.Add(std::move(callback));
}

bool AutomaticOrganizer::CanAskForName() const {
  return !profile()->IsOffTheRecord() &&
         IsComplete(ReadAiConnectionConfig(profile()->GetPrefs()));
}

void AutomaticOrganizer::AskForName(const std::string& task) {
  if (!CanAskForName() || IsNaming(task)) {
    return;
  }
  // The request describes the first tab of the task and lists the others.
  const Page* first = nullptr;
  Request request;
  for (const Page& page : controller_->tree().pages()) {
    const auto activity = pages_.find(page.id);
    if (page.task == task && activity != pages_.end() &&
        !activity->second.title.empty()) {
      first = &page;
      request.context.title = base::UTF16ToUTF8(activity->second.title);
      request.context.url = activity->second.url.spec();
      request.context.source_description = "no source tab";
      break;
    }
  }
  std::unique_ptr<RemoteModelClient> client =
      first ? PrepareNaming(request, MemberTitles(*first)) : nullptr;
  if (!client) {
    NameFailed(task, std::u16string());
    return;
  }
  RemoteModelClient* sender = client.get();
  asked_names_[task] = AskedName{std::move(client), std::move(request.naming)};
  UpdateNaming();
  sender->Send(base::BindOnce(&AutomaticOrganizer::OnAskedName,
                              weak_factory_.GetWeakPtr(), task));
}

void AutomaticOrganizer::DropAskedName(const std::string& task) {
  if (asked_names_.erase(task)) {
    UpdateNaming();
  }
}

void AutomaticOrganizer::NameFailed(const std::string& task,
                                    std::u16string reason) {
  Event event{Event::Kind::kNameFailed, std::move(reason),
              controller_->TaskTitle(task)};
  event.task = task;
  events_.Notify(event);
}

AutomaticOrganizer::TidyRequest::TidyRequest() = default;
AutomaticOrganizer::TidyRequest::~TidyRequest() = default;

void AutomaticOrganizer::Tidy() {
  if (!CanTidy() || IsTidying()) {
    return;
  }
  const PageTree& tree = controller_->tree();
  auto request = std::make_unique<TidyRequest>();
  std::vector<TidyTab> tabs;
  std::map<PageId, size_t> numbers;
  for (const Page& page : tree.pages()) {
    const auto activity = pages_.find(page.id);
    if (!page.task.empty() || controller_->IsKept(page.id) ||
        activity == pages_.end() || activity->second.title.empty()) {
      continue;
    }
    const auto parent = numbers.find(page.parent);
    tabs.push_back({base::UTF16ToUTF8(activity->second.title),
                    activity->second.url.spec(),
                    activity->second.folder.value(),
                    parent == numbers.end() ? 0 : parent->second});
    numbers[page.id] = tabs.size();
    request->tabs.push_back(page.id);
    request->revisions.push_back(activity->second.revision);
    request->parents.push_back(page.parent);
  }
  if (tabs.empty()) {
    Event event{Event::Kind::kTidyNothing,
                l10n_util::GetStringUTF16(IDS_UW_TIDY_NOTHING_DETAIL)};
    events_.Notify(event);
    return;
  }
  std::vector<TidyTask> tasks;
  for (const std::string& task : tree.Tasks()) {
    if (!Named(task)) {
      continue;
    }
    TidyTask offered{base::UTF16ToUTF8(controller_->TaskTitle(task)), {}};
    for (const Page& page : tree.pages()) {
      const auto activity = pages_.find(page.id);
      if (page.task == task && activity != pages_.end() &&
          !activity->second.title.empty()) {
        offered.titles.push_back(base::UTF16ToUTF8(activity->second.title));
      }
    }
    tasks.push_back(std::move(offered));
    request->tasks.push_back(task);
  }
  const AiConnectionConfig language =
      ReadAiConnectionConfig(profile()->GetPrefs());
  std::optional<std::string> body =
      BuildTidyRequestBody(language.model, tabs, tasks);
  request->client = body ? Client(language, *body) : nullptr;
  if (!request->client) {
    TidyFailed(l10n_util::GetStringUTF16(IDS_UW_ACTIVITY_ANSWER_UNREADABLE));
    return;
  }
  request->exchange.model = language.model;
  request->exchange.url = LanguageModelUrl(language.endpoint, language.format);
  request->exchange.body = std::move(*body);
  request->exchange.sent_at = base::TimeTicks::Now();
  RemoteModelClient* sender = request->client.get();
  tidy_ = std::move(request);
  Event event{Event::Kind::kTidying,
              l10n_util::GetStringFUTF16(IDS_UW_ACTIVITY_ASKING,
                                         base::UTF8ToUTF16(language.model))};
  event.model = base::UTF8ToUTF16(language.model);
  event.tabs = tabs.size();
  events_.Notify(event);
  sender->Send(
      base::BindOnce(&AutomaticOrganizer::OnTidy, weak_factory_.GetWeakPtr()));
}

void AutomaticOrganizer::OnTidy(RemoteModelClient::Outcome outcome) {
  std::unique_ptr<TidyRequest> request = std::move(tidy_);
  if (!request) {
    return;
  }
  ScheduleSettle();
  request->exchange.took = base::TimeTicks::Now() - request->exchange.sent_at;
  const std::optional<std::vector<TidyChoice>> choices =
      outcome.ok ? ParseTidyResponse(outcome.body, request->tabs.size(),
                                     request->tasks.size())
                 : std::nullopt;
  const std::u16string reason =
      outcome.ok ? l10n_util::GetStringUTF16(IDS_UW_ACTIVITY_ANSWER_UNREADABLE)
                 : l10n_util::GetStringFUTF16(IDS_UW_ACTIVITY_REQUEST_FAILED,
                                              FailureReason(outcome));
  request->exchange.answer = std::move(outcome);
  if (!choices) {
    LogOrganizerExchange("tidy", request->exchange, "no usable answer");
    TidyFailed(reason);
    return;
  }

  const PageTree before = controller_->tree();
  const std::vector<std::string> open_tasks = before.Tasks();
  std::map<PageId, TidyTarget> targets;
  std::set<PageId> held;
  for (size_t i = 0; i < request->tabs.size(); ++i) {
    const PageId& id = request->tabs[i];
    const Page* page = before.Find(id);
    const auto activity = pages_.find(id);
    if (!page || !page->task.empty() || page->parent != request->parents[i] ||
        controller_->IsKept(id) || activity == pages_.end() ||
        activity->second.revision != request->revisions[i]) {
      continue;
    }
    const TidyChoice& choice = (*choices)[i];
    TidyTarget target{std::string(), choice.new_task};
    if (choice.task) {
      target.task = request->tasks[choice.task - 1];
      if (!std::ranges::contains(open_tasks, target.task)) {
        continue;
      }
    }
    targets[id] = std::move(target);
    if (controller_->IsHeld(id)) {
      held.insert(id);
    }
    // Tidy has decided about the tab. It is asked about again once it shows
    // something else.
    activity->second.placed = true;
    kept_.erase(id);
  }
  const std::vector<TidyMove> moves = PlanTidy(before, targets, held);
  controller_->ApplyTidy(moves);
  LogOrganizerExchange("tidy", request->exchange,
                       moves.empty() ? "nothing moved" : "moved");
  if (moves.empty()) {
    Event event{Event::Kind::kTidyNothing,
                l10n_util::GetStringUTF16(IDS_UW_TIDY_NOTHING_DETAIL)};
    events_.Notify(event);
    return;
  }

  Event event{Event::Kind::kTidied};
  std::vector<std::u16string> names;
  for (const TidyMove& move : moves) {
    std::u16string name = move.target.task.empty()
                              ? move.target.new_task
                              : controller_->TaskTitle(move.target.task);
    if (!std::ranges::contains(names, name)) {
      names.push_back(std::move(name));
    }
  }
  for (const Page& page : controller_->tree().pages()) {
    const Page* earlier = before.Find(page.id);
    if (earlier && earlier->task.empty() && !page.task.empty()) {
      ++event.tabs;
    }
  }
  event.tasks = names.size();
  event.page = base::JoinString(names, u", ");
  events_.Notify(event);
}

void AutomaticOrganizer::TidyFailed(std::u16string reason) {
  Event event{Event::Kind::kTidyFailed, std::move(reason)};
  events_.Notify(event);
}

base::WeakPtr<AutomaticOrganizer> AutomaticOrganizer::AsWeakPtr() {
  return weak_factory_.GetWeakPtr();
}

void AutomaticOrganizer::DispatchNowForTesting() {
  timer_.Stop();
  Dispatch();
}

void AutomaticOrganizer::OnTabChangedAt(tabs::TabInterface*, TabChangeType) {
  Scan();
}

Profile* AutomaticOrganizer::profile() const {
  return window_->GetProfile();
}

bool AutomaticOrganizer::Enabled() const {
  const PrefService* prefs = profile()->GetPrefs();
  return !profile()->IsOffTheRecord() && OrganizationReady(prefs) &&
         ReadPauseState(prefs) == PauseMode::kOff;
}

void AutomaticOrganizer::OnConfigChanged() {
  if (!Enabled()) {
    timer_.Stop();
    requests_.clear();
    kept_.clear();
    asked_.clear();
    for (auto& [id, activity] : pages_) {
      activity.asking = false;
    }
    UpdateNaming();
  }
}

void AutomaticOrganizer::Scan() {
  const PageTree& tree = controller_->tree();
  const size_t known = pages_.size() + requests_.size();
  std::erase_if(pages_,
                [&tree](const auto& entry) { return !tree.Find(entry.first); });
  std::erase_if(requests_, [&tree](const auto& entry) {
    return !tree.Find(entry.second.request.page);
  });
  std::erase_if(kept_,
                [&tree](const auto& entry) { return !tree.Find(entry.first); });
  std::erase_if(asked_, [&tree](const PageId& id) { return !tree.Find(id); });
  // A page that closed may be what the rest of its branch waited for.
  if (pages_.size() + requests_.size() != known) {
    UpdateNaming();
    ScheduleSettle();
  }
  const bool enabled = Enabled();
  bool due = false;
  for (const Page& page : tree.pages()) {
    const tabs::TabInterface* tab = controller_->TabFor(page.id);
    content::WebContents* contents = tab ? tab->GetContents() : nullptr;
    if (!contents) {
      continue;
    }
    // A page that is still loading and has no title yet shows only its
    // address. It is looked at once it has a title or has finished loading.
    const content::NavigationEntry* entry =
        contents->GetController().GetLastCommittedEntry();
    if (contents->IsLoading() && (!entry || entry->GetTitle().empty())) {
      continue;
    }
    const GURL& url = contents->GetLastCommittedURL();
    const bool terminal = IsTerminalUrl(url);
    const base::FilePath folder =
        terminal ? TerminalFolder(contents) : base::FilePath();
    // A terminal tab is looked at once its shell has said where it is.
    if (terminal && folder.empty()) {
      continue;
    }
    Activity& activity = pages_[page.id];
    const std::u16string& title = contents->GetTitle();
    const bool first_sight = activity.revision == 0;
    const bool changed = activity.title != title || activity.url != url ||
                         activity.folder != folder;
    // A shell sets a new title for every command, so a terminal tab shows
    // something else only in another folder.
    const bool shows_else =
        first_sight ||
        (changed &&
         (terminal ? activity.folder != folder
                   : ShowsSomethingElse(page, activity, entry, title, url)));
    activity.url = url;
    activity.folder = folder;
    activity.title = title;
    if (controller_->IsKept(page.id) || controller_->IsHeld(page.id)) {
      // A kept or held tab is never asked about. A request already out for it
      // is dropped, and its branch need not wait for it.
      if (first_sight || activity.asking ||
          activity.requested < activity.revision) {
        CancelRequestsFor(page.id);
        activity.requested = ++activity.revision;
        activity.asking = false;
        ScheduleSettle();
      }
      activity.placed = true;
      activity.by_hand = true;
      kept_.erase(page.id);
      NoteAsked(page.id, activity);
      continue;
    }
    if (!shows_else) {
      continue;
    }
    ++activity.revision;
    activity.asking = false;
    activity.placed = false;
    activity.by_hand = false;
    kept_.erase(page.id);
    // A page is filed when it opens here or changes, not for being there:
    // restored tabs and tabs opened while organization is off keep their
    // place.
    if (!enabled || (first_sight && !controller_->WasOpenedHere(page.id))) {
      activity.requested = activity.revision;
      NoteAsked(page.id, activity);
      continue;
    }
    CancelRequestsFor(page.id);
    due = true;
  }
  if (due) {
    timer_.Start(
        FROM_HERE, kDebounce,
        base::BindOnce(&AutomaticOrganizer::Dispatch, base::Unretained(this)));
  }
}

bool AutomaticOrganizer::ShowsSomethingElse(
    const Page& page,
    const Activity& activity,
    const content::NavigationEntry* entry,
    const std::u16string& title,
    const GURL& url) const {
  if (!url::IsSameOriginWith(activity.asked_url, url)) {
    return true;
  }
  // A reload, or a return to what uw last asked about, is nothing new.
  const bool new_title = title != activity.asked_title;
  const bool new_address = url != activity.asked_url;
  if (!new_title && !new_address) {
    return false;
  }
  if (new_title && entry && entry->GetUniqueID() != activity.asked_entry &&
      StartedByUser(*entry)) {
    return true;
  }
  // On the same site a tab in a task stays the same piece of work. A tab in
  // the Scratchpad, or a hub, shows what it is for as the user moves on in
  // it. A page that only updates its own title, such as a clock or an unread
  // count, keeps its address.
  return !activity.by_hand && (page.task.empty() || activity.hub) &&
         new_title && new_address;
}

void AutomaticOrganizer::NoteAsked(const PageId& page,
                                   Activity& activity) const {
  const tabs::TabInterface* tab = controller_->TabFor(page);
  const content::NavigationEntry* entry =
      tab && tab->GetContents()
          ? tab->GetContents()->GetController().GetLastCommittedEntry()
          : nullptr;
  activity.asked_title = activity.title;
  activity.asked_url = activity.url;
  activity.asked_entry = entry ? entry->GetUniqueID() : 0;
}

void AutomaticOrganizer::OnManualMove(const std::vector<PageId>& pages) {
  ++moves_;
  for (const PageId& page : pages) {
    const auto activity = pages_.find(page);
    if (activity != pages_.end()) {
      activity->second.requested = ++activity->second.revision;
      activity->second.asking = false;
      activity->second.placed = true;
      activity->second.by_hand = true;
      NoteAsked(page, activity->second);
    }
    kept_.erase(page);
    CancelRequestsFor(page);
  }
  // The rest of a branch may have waited for a request that is now dropped.
  ScheduleSettle();
}

void AutomaticOrganizer::Dispatch() {
  if (!Enabled()) {
    return;
  }
  for (auto& [id, activity] : pages_) {
    if (activity.requested >= activity.revision) {
      continue;
    }
    activity.requested = activity.revision;
    NoteAsked(id, activity);
    const tabs::TabInterface* tab = controller_->TabFor(id);
    content::WebContents* contents = tab ? tab->GetContents() : nullptr;
    content::RenderFrameHost* frame =
        contents ? contents->GetPrimaryMainFrame() : nullptr;
    // A terminal tab is described by its title and folder, never by what
    // the shell has printed.
    if (frame && IsTerminalUrl(activity.url)) {
      activity.asking = true;
      base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
          FROM_HERE,
          base::BindOnce(&AutomaticOrganizer::Ask, weak_factory_.GetWeakPtr(),
                         id, activity.revision, std::string()));
      continue;
    }
    if (!frame || !activity.url.SchemeIsHTTPOrHTTPS()) {
      // Nothing is asked about this page, so its branch need not wait for it.
      ScheduleSettle();
      continue;
    }
    activity.asking = true;
    content_extraction::GetInnerText(
        *frame, std::nullopt,
        base::BindOnce(&AutomaticOrganizer::OnExcerpt,
                       weak_factory_.GetWeakPtr(), id, activity.revision));
  }
}

void AutomaticOrganizer::OnExcerpt(
    PageId page,
    uint64_t revision,
    std::unique_ptr<content_extraction::InnerTextResult> result) {
  std::string excerpt;
  if (result) {
    base::TruncateUTF8ToByteSize(
        base::CollapseWhitespaceASCII(result->inner_text, true),
        kMaxExcerptLength, &excerpt);
  }
  Ask(page, revision, std::move(excerpt));
}

void AutomaticOrganizer::Ask(PageId page,
                             uint64_t revision,
                             std::string excerpt) {
  if (SendDecision(page, revision, std::move(excerpt))) {
    return;
  }
  const auto activity = pages_.find(page);
  if (activity != pages_.end() && activity->second.revision == revision) {
    activity->second.asking = false;
  }
  ScheduleSettle();
}

bool AutomaticOrganizer::SendDecision(const PageId& id,
                                      uint64_t revision,
                                      std::string excerpt) {
  const Page* page = controller_->tree().Find(id);
  const auto activity = pages_.find(id);
  const tabs::TabInterface* tab = controller_->TabFor(id);
  content::WebContents* contents = tab ? tab->GetContents() : nullptr;
  if (!Enabled() || !page || !contents || activity == pages_.end() ||
      activity->second.revision != revision) {
    return false;
  }
  CancelRequestsFor(id);
  NoteAsked(id, activity->second);
  const PrefService* prefs = profile()->GetPrefs();
  const DecisionModelConfig decision = ReadDecisionModelConfig(prefs);
  const AiConnectionConfig language = ReadAiConnectionConfig(prefs);
  const bool decides = IsComplete(decision);

  Request request;
  request.page = id;
  request.revision = revision;
  request.task = page->task;
  request.task_title = controller_->TaskTitle(page->task);
  request.parent = page->parent;
  request.format = decides ? decision.format : DecisionFormat::kChat;
  request.context.url = activity->second.url.spec();
  request.context.title = activity->second.title.empty()
                              ? request.context.url
                              : base::UTF16ToUTF8(activity->second.title);
  const auto parent = pages_.find(page->parent);
  request.context.source_description =
      parent == pages_.end() || !FollowedLinksOnly(contents)
          ? "no source tab"
          : "opened from \"" + base::UTF16ToUTF8(parent->second.title) + "\"";
  request.context.excerpt = std::move(excerpt);
  request.context.folder = ShortFolder(activity->second.folder);
  request.in_unnamed_task = !request.task.empty() && !Named(request.task);

  std::vector<std::pair<std::string, std::string>> tasks;
  for (const std::string& task : controller_->tree().Tasks()) {
    if (Named(task)) {
      tasks.emplace_back(task, base::UTF16ToUTF8(controller_->TaskTitle(task)));
    }
  }
  request.has_tasks = !tasks.empty();
  const std::string& model = decides ? decision.model : language.model;
  const std::optional<NameCheck> name_check =
      decides ? NameCheckFor(*page) : std::nullopt;
  request.checks_name = name_check.has_value();
  std::optional<std::string> body = BuildDecisionRequestBody(
      request.format, model, request.context, tasks, name_check);
  if (!body) {
    return false;
  }
  request.decision.model = model;
  request.decision.url =
      decides ? DecisionUrl(decision)
              : LanguageModelUrl(language.endpoint, language.format);
  request.decision.body = *body;
  request.decision.sent_at = base::TimeTicks::Now();
  std::unique_ptr<RemoteModelClient> client =
      decides
          ? Client(request.decision.url, decision.key_account, std::move(*body))
          : Client(language, std::move(*body));
  if (!client) {
    return false;
  }
  Report(Event::Kind::kAsking, request);
  Send(std::move(request), std::move(client), &AutomaticOrganizer::OnDecision);
  return true;
}

std::vector<std::string> AutomaticOrganizer::MemberTitles(
    const Page& page) const {
  const bool named = Named(page.task);
  std::vector<std::string> titles;
  for (const Page& other : controller_->tree().pages()) {
    const auto activity = pages_.find(other.id);
    if (other.task != page.task || other.id == page.id ||
        activity == pages_.end() || activity->second.title.empty()) {
      continue;
    }
    const auto kept = kept_.find(other.id);
    if (named &&
        (activity->second.asking ||
         activity->second.requested < activity->second.revision ||
         (kept != kept_.end() && kept->second.answer.choice != page.task))) {
      continue;
    }
    titles.push_back(base::UTF16ToUTF8(activity->second.title));
  }
  return titles;
}

std::vector<std::string> AutomaticOrganizer::TitlesUnder(
    const PageId& top) const {
  std::vector<std::string> titles;
  for (const PageId& id : controller_->tree().BranchOf(top)) {
    const auto activity = pages_.find(id);
    if (id != top && activity != pages_.end() &&
        !activity->second.title.empty()) {
      titles.push_back(base::UTF16ToUTF8(activity->second.title));
    }
  }
  return titles;
}

std::optional<NameCheck> AutomaticOrganizer::NameCheckFor(
    const Page& page) const {
  if (page.task.empty()) {
    return std::nullopt;
  }
  std::vector<std::string> titles = MemberTitles(page);
  if (titles.empty()) {
    return std::nullopt;
  }
  return NameCheck{std::move(titles)};
}

void AutomaticOrganizer::NameTaskOf(Request request,
                                    std::optional<std::u16string> name) {
  const Page* page = controller_->tree().Find(request.page);
  request.names_task = true;
  // One request for a name is enough.
  if (IsNaming(request.task)) {
    Report(Event::Kind::kLeft, request);
    return;
  }
  owed_names_.erase(request.task);
  std::unique_ptr<RemoteModelClient> client;
  if (!name && page) {
    client = PrepareNaming(request, MemberTitles(*page));
  }
  if (!name && !client) {
    Report(Event::Kind::kLeft, request);
    return;
  }
  Report(Event::Kind::kNeedsName, request);
  if (name) {
    controller_->NameTask(request.task, *name);
    return;
  }
  Send(std::move(request), std::move(client), &AutomaticOrganizer::OnName);
}

std::unique_ptr<RemoteModelClient> AutomaticOrganizer::PrepareNaming(
    Request& request,
    const std::vector<std::string>& other_titles) {
  const AiConnectionConfig language =
      ReadAiConnectionConfig(profile()->GetPrefs());
  std::optional<std::string> body =
      IsComplete(language) ? BuildNamingRequestBody(
                                 language.model, request.context, other_titles)
                           : std::nullopt;
  if (!body) {
    return nullptr;
  }
  std::unique_ptr<RemoteModelClient> client = Client(language, *body);
  if (!client) {
    return nullptr;
  }
  request.naming.model = language.model;
  request.naming.url = LanguageModelUrl(language.endpoint, language.format);
  request.naming.body = std::move(*body);
  request.naming.sent_at = base::TimeTicks::Now();
  return client;
}

void AutomaticOrganizer::Send(Request request,
                              std::unique_ptr<RemoteModelClient> client,
                              AnswerHandler on_answer) {
  const uint64_t request_id = next_request_++;
  RemoteModelClient* sender = client.get();
  requests_.emplace(request_id, Pending{std::move(request), std::move(client)});
  UpdateNaming();
  sender->Send(
      base::BindOnce(on_answer, weak_factory_.GetWeakPtr(), request_id));
}

void AutomaticOrganizer::OnDecision(uint64_t request_id,
                                    RemoteModelClient::Outcome outcome) {
  std::optional<Pending> pending = Take(request_id);
  if (!pending) {
    return;
  }
  Request& request = pending->request;
  const PageId page = request.page;
  request.decision.took = base::TimeTicks::Now() - request.decision.sent_at;
  request.decision.answer = outcome;
  const auto activity = pages_.find(page);
  if (activity != pages_.end() &&
      activity->second.revision == request.revision) {
    activity->second.asking = false;
  }
  std::optional<ClassificationAnswer> answer =
      outcome.ok ? ParseDecisionResponse(request.format, outcome.body,
                                         request.has_tasks, request.checks_name)
                 : std::nullopt;
  if (answer && answer->choice != kNewTaskOption &&
      answer->choice != kScratchpadOption && !Named(answer->choice)) {
    answer.reset();
  }
  if (!Current(request)) {
    Report(Event::Kind::kDropped, request);
  } else if (!outcome.ok) {
    Report(Event::Kind::kRequestFailed, request, FailureReason(outcome));
  } else if (!answer) {
    Report(Event::Kind::kAnswerUnreadable, request);
  } else {
    request.new_name = answer->same_work;
    activity->second.hub = IsHub(*answer);
    kept_.insert_or_assign(page, Kept{std::move(request), std::move(*answer)});
  }
  Settle(page);
}

void AutomaticOrganizer::Settle(const PageId& page) {
  if (!Enabled()) {
    return;
  }
  const PageTree& tree = controller_->tree();
  const PageId top = tree.TopOf(page);
  if (!tree.Find(top)) {
    return;
  }
  // Tidy's answer decides about the Scratchpad's tabs first.
  if (IsTidying() && tree.Find(top)->task.empty()) {
    return;
  }
  const std::vector<PageId> branch = tree.BranchOf(top);
  for (const PageId& id : branch) {
    const auto activity = pages_.find(id);
    if (activity != pages_.end() &&
        (activity->second.asking ||
         activity->second.requested < activity->second.revision)) {
      return;
    }
  }
  std::vector<BranchPlacement> placements = PlaceBranchOf(top);
  const auto moves = [](const BranchPlacement& group) {
    return group.placement.action == Placement::Action::kApply;
  };
  // Before a tab moves or its task gets a name, the tabs of the branch that
  // nobody asked about are asked about, once.
  const bool acts =
      std::ranges::any_of(placements, [](const BranchPlacement& group) {
        return group.placement.action == Placement::Action::kApply ||
               group.placement.action == Placement::Action::kName;
      });
  if (acts && asked_.insert(top).second && AskRest(branch)) {
    return;
  }
  asked_.erase(top);
  // Moves come last, so the sidebar ends on what changed.
  std::ranges::stable_partition(placements, std::not_fn(moves));
  for (const BranchPlacement& group : placements) {
    CarryOut(group);
  }
}

void AutomaticOrganizer::SettleKept() {
  settle_pending_ = false;
  std::set<PageId> tops;
  for (const auto& [id, kept] : kept_) {
    tops.insert(controller_->tree().TopOf(id));
  }
  for (const PageId& top : tops) {
    Settle(top);
  }
}

void AutomaticOrganizer::ScheduleSettle() {
  if (settle_pending_) {
    return;
  }
  settle_pending_ = true;
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(&AutomaticOrganizer::SettleKept,
                                weak_factory_.GetWeakPtr()));
}

std::vector<BranchPlacement> AutomaticOrganizer::PlaceBranchOf(
    const PageId& top) {
  const PageTree& tree = controller_->tree();
  const std::string task = tree.Find(top)->task;
  std::map<PageId, TabAnswer> tabs;
  for (const PageId& id : tree.BranchOf(top)) {
    const auto activity = pages_.find(id);
    const bool kept_tab = controller_->IsKept(id);
    if (activity == pages_.end() && !kept_tab) {
      continue;
    }
    TabAnswer& tab = tabs[id];
    tab.placed = kept_tab || activity->second.placed;
    // A kept tab stays where the user left it, even when it is held.
    tab.held = !kept_tab && controller_->IsHeld(id);
    if (activity == pages_.end() || tab.held) {
      continue;
    }
    const auto kept = kept_.find(id);
    if (kept == kept_.end()) {
      continue;
    }
    if (Valid(kept->second)) {
      tab.answer = kept->second.answer;
    } else {
      Report(Event::Kind::kDropped, kept->second.request);
      kept_.erase(kept);
    }
  }
  const bool named = Named(task);
  return PlaceBranch(tree, top, tabs,
                     {named ? task : std::string(), !task.empty() && !named,
                      named && !Generated(task)},
                     ReadThresholds(profile()->GetPrefs()));
}

bool AutomaticOrganizer::AskRest(const std::vector<PageId>& branch) {
  bool asked = false;
  for (const PageId& id : branch) {
    const auto activity = pages_.find(id);
    if (activity == pages_.end() || activity->second.placed ||
        kept_.contains(id) || !activity->second.url.SchemeIsHTTPOrHTTPS()) {
      continue;
    }
    activity->second.requested = activity->second.revision - 1;
    asked = true;
  }
  if (asked) {
    Dispatch();
  }
  return asked;
}

void AutomaticOrganizer::CarryOut(const BranchPlacement& group) {
  using Action = Placement::Action;
  const auto top = kept_.find(group.top);
  if (top == kept_.end()) {
    return;
  }
  const bool owed = group.placement.action == Action::kLeave &&
                    top->second.request.in_unnamed_task &&
                    owed_names_.contains(top->second.request.task);
  // A task whose name could not be had gets it from the next tab that stays.
  const Action action = owed ? Action::kName : group.placement.action;
  // The requests of the group that the log does not hold yet.
  std::vector<Kept*> fresh;
  for (const PageId& id : group.pages) {
    const auto kept = kept_.find(id);
    if (kept == kept_.end()) {
      continue;
    }
    Request& request = kept->second.request;
    if (!request.logged || id == group.top) {
      request.target = group.placement.target;
      request.confidence = group.placement.confidence;
      request.needed = group.placement.needed;
    }
    if (!request.logged) {
      fresh.push_back(&kept->second);
    }
  }
  if (action == Action::kLeave || action == Action::kSuggest) {
    if (action == Action::kSuggest && !fresh.empty()) {
      suggestion_ =
          Suggestion{top->second.request, top->second.answer.task_name};
      suggestion_->request.staying = group.staying;
      suggestion_->request.logged = true;
    }
    for (Kept* kept : fresh) {
      if (action == Action::kLeave) {
        Report(Event::Kind::kLeft, kept->request);
      } else {
        Report(Event::Kind::kUnsure, kept->request, TargetName(kept->request));
      }
    }
    return;
  }
  // A move is about the top tab of the group. A name is asked for with the
  // tab that brought it up: the one whose answer is new. The copy reports for
  // the kept request from here on.
  Kept& lead =
      action == Action::kName && top->second.request.logged && !fresh.empty()
          ? *fresh.front()
          : top->second;
  Request request = lead.request;
  request.staying = group.staying;
  lead.request.logged = true;
  std::optional<std::u16string> task_name = lead.answer.task_name;
  if (action == Action::kName) {
    const Request named = request;
    NameTaskOf(std::move(request), std::move(task_name));
    Placed(group.pages, Event::Kind::kNeedsName, named);
    return;
  }
  Apply(std::move(request), std::move(task_name));
}

void AutomaticOrganizer::AcceptSuggestion() {
  if (!suggestion_) {
    return;
  }
  Suggestion suggestion = std::move(*suggestion_);
  suggestion_.reset();
  if (!Current(suggestion.request)) {
    Report(Event::Kind::kDropped, suggestion.request);
    return;
  }
  LogOrganizerExchange("accepted", suggestion.request.decision,
                       suggestion.request.target,
                       LogDetails(suggestion.request));
  Apply(std::move(suggestion.request), std::move(suggestion.task_name));
}

void AutomaticOrganizer::Apply(Request request,
                               std::optional<std::u16string> task_name) {
  if (request.target != kNewTaskOption) {
    const std::optional<tab_groups::TabGroupId> group =
        controller_->GroupFor(request.target);
    const bool moved =
        request.target == kScratchpadOption
            ? controller_->MoveToScratchpad(
                  {request.page}, TabOrganizationController::Origin::kOrganizer,
                  request.staying)
            : group && controller_->MoveToTask(
                           {request.page}, *group,
                           TabOrganizationController::Origin::kOrganizer,
                           request.staying);
    Report(moved ? Event::Kind::kMoved : Event::Kind::kDropped, request,
           TargetName(request));
    if (moved) {
      ++moves_;
      Placed(controller_->tree().BranchOf(request.page), Event::Kind::kMoved,
             request);
    } else {
      kept_.erase(request.page);
    }
    return;
  }
  // A new task starts under the name every new task has, so the tabs move at
  // once. The language model is asked for the name afterwards.
  const std::u16string title =
      task_name.value_or(l10n_util::GetStringUTF16(IDS_UW_UNTITLED_TASK));
  const std::optional<tab_groups::TabGroupId> task = controller_->MoveToNewTask(
      {request.page}, title, TabOrganizationController::Origin::kOrganizer,
      request.staying);
  if (!task) {
    Report(Event::Kind::kDropped, request);
    kept_.erase(request.page);
    return;
  }
  request.moves = ++moves_;
  std::unique_ptr<RemoteModelClient> client =
      task_name ? nullptr : PrepareNaming(request, TitlesUnder(request.page));
  if (client) {
    Report(Event::Kind::kNaming, request,
           base::UTF8ToUTF16(request.naming.model));
  } else {
    Report(Event::Kind::kCreated, request, title);
  }
  Placed(controller_->tree().BranchOf(request.page), Event::Kind::kCreated,
         request);
  if (!client) {
    return;
  }
  // From here on the request is about the page in the task it started.
  request.task = task->ToString();
  request.parent = PageId();
  Send(std::move(request), std::move(client), &AutomaticOrganizer::OnName);
}

void AutomaticOrganizer::OnName(uint64_t request_id,
                                RemoteModelClient::Outcome outcome) {
  std::optional<Pending> pending = Take(request_id);
  if (!pending) {
    return;
  }
  Request& request = pending->request;
  std::optional<std::u16string> name =
      outcome.ok ? ParseNamingResponse(outcome.body) : std::nullopt;
  request.naming.took = base::TimeTicks::Now() - request.naming.sent_at;
  request.naming.answer = std::move(outcome);
  LogOrganizerExchange("name", request.naming,
                       name ? "named" : "no usable name");
  const bool current = Current(request);
  if (!name) {
    owed_names_.insert(request.task);
  } else if (current && (!Named(request.task) || Generated(request.task))) {
    controller_->NameTask(request.task, *name);
  }
  // The card that waited for the name of a new task now says what the task
  // is called and offers Undo. After another move, Undo would reverse that
  // move, so the card stays as it is.
  if (!request.names_task && current && request.moves == moves_) {
    Report(Event::Kind::kCreated, request,
           controller_->TaskTitle(request.task));
  }
}

void AutomaticOrganizer::OnAskedName(const std::string& task,
                                     RemoteModelClient::Outcome outcome) {
  auto found = asked_names_.extract(task);
  if (found.empty()) {
    return;
  }
  AskedName asked = std::move(found.mapped());
  UpdateNaming();
  const std::optional<std::u16string> name =
      outcome.ok ? ParseNamingResponse(outcome.body) : std::nullopt;
  const std::u16string reason =
      outcome.ok ? l10n_util::GetStringUTF16(IDS_UW_ACTIVITY_ANSWER_UNREADABLE)
                 : l10n_util::GetStringFUTF16(IDS_UW_ACTIVITY_REQUEST_FAILED,
                                              FailureReason(outcome));
  asked.exchange.took = base::TimeTicks::Now() - asked.exchange.sent_at;
  asked.exchange.answer = std::move(outcome);
  LogOrganizerExchange("name", asked.exchange,
                       name ? "named" : "no usable name");
  if (name) {
    controller_->NameTask(task, *name);
  } else {
    NameFailed(task, reason);
  }
}

void AutomaticOrganizer::Placed(const std::vector<PageId>& pages,
                                Event::Kind kind,
                                const Request& by) {
  for (const PageId& id : pages) {
    const auto activity = pages_.find(id);
    if (activity != pages_.end()) {
      activity->second.placed = true;
    }
    const auto kept = kept_.find(id);
    if (kept == kept_.end()) {
      continue;
    }
    Request& request = kept->second.request;
    if (!request.logged) {
      request.target = by.target;
      request.confidence = by.confidence;
      request.needed = by.needed;
      LogOrganizerExchange("placement", request.decision, ResultName(kind),
                           LogDetails(request));
    }
    kept_.erase(kept);
  }
}

void AutomaticOrganizer::Report(Event::Kind kind,
                                Request& request,
                                std::u16string detail) {
  if (kind != Event::Kind::kAsking && !request.logged) {
    LogOrganizerExchange("placement", request.decision, ResultName(kind),
                         LogDetails(request));
    request.logged = true;
  }
  events_.Notify(Event{kind, base::UTF8ToUTF16(request.context.title),
                       std::move(detail), request.confidence, request.needed,
                       base::UTF8ToUTF16(request.decision.model),
                       request.decision.took});
}

std::u16string AutomaticOrganizer::TargetName(const Request& request) const {
  if (request.target == kNewTaskOption) {
    return l10n_util::GetStringUTF16(IDS_UW_ACTIVITY_NEW_TASK_CHOICE);
  }
  return request.target == kScratchpadOption
             ? l10n_util::GetStringUTF16(IDS_UW_SCRATCHPAD)
             : controller_->TaskTitle(request.target);
}

base::DictValue AutomaticOrganizer::LogDetails(const Request& request) {
  base::DictValue details;
  details.Set("page", request.page.value());
  details.Set("from", request.task);
  details.Set("to", request.target);
  details.Set("confidence", request.confidence);
  details.Set("needed", request.needed);
  details.Set("new_name", request.new_name);
  return details;
}

bool AutomaticOrganizer::Current(const Request& request) const {
  const Page* page = controller_->tree().Find(request.page);
  const auto activity = pages_.find(request.page);
  return Enabled() && page && activity != pages_.end() &&
         activity->second.revision == request.revision &&
         page->task == request.task && page->parent == request.parent;
}

bool AutomaticOrganizer::Valid(const Kept& kept) const {
  const Request& request = kept.request;
  const Page* page = controller_->tree().Find(request.page);
  const auto activity = pages_.find(request.page);
  return page && activity != pages_.end() &&
         activity->second.revision == request.revision &&
         page->task == request.task &&
         controller_->TaskTitle(request.task) == request.task_title;
}

bool AutomaticOrganizer::Named(const std::string& task) const {
  const std::u16string title = controller_->TaskTitle(task);
  return !title.empty() &&
         title != l10n_util::GetStringUTF16(IDS_UW_UNTITLED_TASK);
}

bool AutomaticOrganizer::Generated(const std::string& task) const {
  return Named(task) && controller_->HasGeneratedName(task);
}

std::optional<AutomaticOrganizer::Pending> AutomaticOrganizer::Take(
    uint64_t request_id) {
  const auto it = requests_.find(request_id);
  if (it == requests_.end()) {
    return std::nullopt;
  }
  Pending pending = std::move(it->second);
  requests_.erase(it);
  UpdateNaming();
  return pending;
}

void AutomaticOrganizer::UpdateNaming() {
  std::set<std::string> naming;
  for (const auto& [id, pending] : requests_) {
    if (!pending.request.naming.body.empty()) {
      naming.insert(pending.request.task);
    }
  }
  for (const auto& [task, asked] : asked_names_) {
    naming.insert(task);
  }
  if (naming != naming_) {
    naming_ = std::move(naming);
    naming_changed_.Notify();
  }
}

void AutomaticOrganizer::CancelRequestsFor(const PageId& page) {
  std::erase_if(requests_, [&](const auto& entry) {
    const Request& request = entry.second.request;
    if (request.page != page) {
      return false;
    }
    // A name that was asked for and will not come is still owed.
    if (!request.naming.body.empty()) {
      owed_names_.insert(request.task);
    }
    return true;
  });
  UpdateNaming();
}

std::unique_ptr<RemoteModelClient> AutomaticOrganizer::Client(
    const GURL& url,
    const std::string& key_account,
    std::string body) {
  std::optional<std::string> key = KeyIn(key_account);
  if (!key) {
    return nullptr;
  }
  return std::make_unique<RemoteModelClient>(
      profile()
          ->GetDefaultStoragePartition()
          ->GetURLLoaderFactoryForBrowserProcess(),
      url, std::move(*key), std::move(body), kProviderTimeout);
}

std::unique_ptr<RemoteModelClient> AutomaticOrganizer::Client(
    const AiConnectionConfig& language,
    const std::string& body) {
  std::optional<std::string> key = KeyIn(language.key_account);
  if (!key) {
    return nullptr;
  }
  return RemoteModelClient::ForLanguageModel(
      profile()
          ->GetDefaultStoragePartition()
          ->GetURLLoaderFactoryForBrowserProcess(),
      language, std::move(*key), body, kProviderTimeout);
}

}  // namespace uw
