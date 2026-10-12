// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/browser/tab_organization/automatic_organizer.h"

#include <utility>

#include "base/functional/bind.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
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
#include "uw/browser/tab_organization/ai_connection.h"
#include "uw/browser/tab_organization/api_key_store.h"
#include "uw/browser/tab_organization/tab_organization_controller.h"

namespace uw {
namespace {

constexpr base::TimeDelta kDebounce = base::Milliseconds(750);

using Kind = AutomaticOrganizer::Event::Kind;

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
    case Kind::kNaming:
      return "new_task";
    case Kind::kAsking:
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

}  // namespace

DEFINE_USER_DATA(AutomaticOrganizer);

AutomaticOrganizer::AutomaticOrganizer(BrowserWindowInterface* window,
                                       TabOrganizationController* controller)
    : window_(window),
      controller_(controller),
      tabs_(window->GetTabStripModel()),
      user_data_(window->GetUnownedUserDataHost(), *this) {
  prefs_.Init(profile()->GetPrefs());
  for (const char* pref :
       {kOrganizationEnabledPref, kOrganizationPauseModePref,
        kOrganizationPauseUntilPref, kOrganizationModePref,
        kOrganizationEndpointPref, kOrganizationModelPref,
        kOrganizationKeyAccountPref, kDecisionProviderPref,
        kDecisionEndpointPref, kDecisionModelPref, kDecisionKeyAccountPref}) {
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
  }
}

void AutomaticOrganizer::Scan() {
  const PageTree& tree = controller_->tree();
  std::erase_if(pages_,
                [&tree](const auto& entry) { return !tree.Find(entry.first); });
  std::erase_if(requests_, [&tree](const auto& entry) {
    return !tree.Find(entry.second.page);
  });
  std::erase_if(generated_names_, [this](const auto& entry) {
    return !controller_->GroupFor(entry.first);
  });
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
    Activity& activity = pages_[page.id];
    const bool first_sight = activity.revision == 0;
    const GURL& url = contents->GetLastCommittedURL();
    // A tab shows something else when its title changes or it goes to another
    // site. A new path or new parameters under the same title are the same
    // page to the organizer: web apps keep their state in the address.
    const bool same_page =
        !first_sight && activity.title == contents->GetTitle() &&
        (activity.url == url || url::IsSameOriginWith(activity.url, url));
    activity.url = url;
    if (same_page) {
      continue;
    }
    activity.title = contents->GetTitle();
    ++activity.revision;
    // A page is filed when it opens here or changes, not for being there:
    // restored tabs and tabs opened while organization is off keep their
    // place.
    if (!enabled || (first_sight && !controller_->WasOpenedHere(page.id))) {
      activity.requested = activity.revision;
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

void AutomaticOrganizer::OnManualMove(const std::vector<PageId>& pages) {
  for (const PageId& page : pages) {
    const auto activity = pages_.find(page);
    if (activity != pages_.end()) {
      activity->second.requested = ++activity->second.revision;
    }
    CancelRequestsFor(page);
  }
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
    const tabs::TabInterface* tab = controller_->TabFor(id);
    content::WebContents* contents = tab ? tab->GetContents() : nullptr;
    content::RenderFrameHost* frame =
        contents ? contents->GetPrimaryMainFrame() : nullptr;
    if (!frame || !activity.url.SchemeIsHTTPOrHTTPS()) {
      continue;
    }
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
  SendDecision(page, revision, std::move(excerpt));
}

void AutomaticOrganizer::SendDecision(const PageId& id,
                                      uint64_t revision,
                                      std::string excerpt) {
  const Page* page = controller_->tree().Find(id);
  const auto activity = pages_.find(id);
  const tabs::TabInterface* tab = controller_->TabFor(id);
  content::WebContents* contents = tab ? tab->GetContents() : nullptr;
  if (!Enabled() || !page || !contents || activity == pages_.end() ||
      activity->second.revision != revision) {
    return;
  }
  CancelRequestsFor(id);
  const PrefService* prefs = profile()->GetPrefs();
  const DecisionModelConfig decision = ReadDecisionModelConfig(prefs);
  const AiConnectionConfig language = ReadAiConnectionConfig(prefs);
  const bool decides = IsComplete(decision);

  Request request;
  request.page = id;
  request.revision = revision;
  request.task = page->task;
  request.parent = page->parent;
  request.format =
      decides ? FormatFor(decision.provider) : DecisionFormat::kChat;
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
    return;
  }
  request.decision.model = model;
  request.decision.url =
      decides ? DecisionUrl(decision) : ChatCompletionsUrl(language.endpoint);
  request.decision.body = *body;
  request.decision.sent_at = base::TimeTicks::Now();
  request.client = Client(request.decision.url,
                          decides ? decision.key_account : language.key_account,
                          std::move(*body));
  if (!request.client) {
    return;
  }
  Report(Event::Kind::kAsking, request);
  Send(std::move(request), &AutomaticOrganizer::OnDecision);
}

std::vector<std::string> AutomaticOrganizer::OtherTitles(
    const Page& page) const {
  std::vector<std::string> titles;
  for (const Page& other : controller_->tree().pages()) {
    const auto activity = pages_.find(other.id);
    if (other.task == page.task && other.id != page.id &&
        activity != pages_.end() && !activity->second.title.empty()) {
      titles.push_back(base::UTF16ToUTF8(activity->second.title));
    }
  }
  return titles;
}

std::optional<NameCheck> AutomaticOrganizer::NameCheckFor(
    const Page& page) const {
  if (!Generated(page.task) ||
      !IsComplete(ReadAiConnectionConfig(profile()->GetPrefs()))) {
    return std::nullopt;
  }
  std::vector<std::string> titles = OtherTitles(page);
  if (titles.empty()) {
    return std::nullopt;
  }
  return NameCheck{base::UTF16ToUTF8(controller_->TaskTitle(page.task)),
                   std::move(titles)};
}

void AutomaticOrganizer::NameTaskOf(Request request,
                                    std::optional<std::u16string> name) {
  const Page* page = controller_->tree().Find(request.page);
  request.names_task = true;
  if (!name && !(page && PrepareNaming(request, OtherTitles(*page)))) {
    Report(Event::Kind::kLeft, request);
    return;
  }
  Report(Event::Kind::kNeedsName, request);
  if (name) {
    GiveName(request.task, *name);
    return;
  }
  Send(std::move(request), &AutomaticOrganizer::OnName);
}

bool AutomaticOrganizer::PrepareNaming(
    Request& request,
    const std::vector<std::string>& other_titles) {
  const AiConnectionConfig language =
      ReadAiConnectionConfig(profile()->GetPrefs());
  std::optional<std::string> body =
      IsComplete(language) ? BuildNamingRequestBody(
                                 language.model, request.context, other_titles)
                           : std::nullopt;
  if (!body) {
    return false;
  }
  request.naming.model = language.model;
  request.naming.url = ChatCompletionsUrl(language.endpoint);
  request.naming.body = *body;
  request.naming.sent_at = base::TimeTicks::Now();
  request.client =
      Client(request.naming.url, language.key_account, std::move(*body));
  return !!request.client;
}

void AutomaticOrganizer::Send(Request request, AnswerHandler on_answer) {
  const uint64_t request_id = next_request_++;
  RemoteModelClient* client = request.client.get();
  requests_.emplace(request_id, std::move(request));
  client->Send(
      base::BindOnce(on_answer, weak_factory_.GetWeakPtr(), request_id));
}

void AutomaticOrganizer::OnDecision(uint64_t request_id,
                                    RemoteModelClient::Outcome outcome) {
  std::optional<Request> request = Take(request_id);
  if (!request) {
    return;
  }
  request->decision.took = base::TimeTicks::Now() - request->decision.sent_at;
  request->decision.answer = outcome;
  if (!Current(*request)) {
    Report(Event::Kind::kDropped, *request);
    return;
  }
  if (!outcome.ok) {
    Report(Event::Kind::kRequestFailed, *request, FailureReason(outcome));
    return;
  }
  std::optional<ClassificationAnswer> answer = ParseDecisionResponse(
      request->format, outcome.body, request->has_tasks, request->checks_name);
  if (!answer ||
      (answer->choice != kNewTaskOption &&
       answer->choice != kScratchpadOption && !Named(answer->choice))) {
    Report(Event::Kind::kAnswerUnreadable, *request);
    return;
  }
  // A page with tabs under it stays in its task unless the user says so:
  // those tabs came from what it showed before.
  const Placement placement =
      Place(*answer,
            {request->in_unnamed_task ? std::string() : request->task,
             request->in_unnamed_task,
             controller_->tree().BranchOf(request->page).size() == 1},
            ThresholdsFor(
                profile()->GetPrefs()->GetInteger(kOrganizationEagernessPref)));
  request->target = placement.target;
  request->confidence = placement.confidence;
  request->needed = placement.needed;
  request->new_name = answer->new_name;
  switch (placement.action) {
    case Placement::Action::kLeave:
      Report(Event::Kind::kLeft, *request);
      return;
    case Placement::Action::kName:
      NameTaskOf(std::move(*request), std::move(answer->task_name));
      return;
    case Placement::Action::kSuggest:
      Report(Event::Kind::kUnsure, *request, TargetName(*request));
      suggestion_ =
          Suggestion{std::move(*request), std::move(answer->task_name)};
      return;
    case Placement::Action::kApply:
      Apply(std::move(*request), std::move(answer->task_name));
  }
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
  if (request.target == kScratchpadOption) {
    const bool moved = controller_->MoveToScratchpad(
        {request.page}, TabOrganizationController::Origin::kOrganizer);
    Report(moved ? Event::Kind::kMoved : Event::Kind::kDropped, request,
           TargetName(request));
    return;
  }
  if (request.target != kNewTaskOption) {
    const std::optional<tab_groups::TabGroupId> group =
        controller_->GroupFor(request.target);
    const bool moved =
        group &&
        controller_->MoveToTask({request.page}, *group,
                                TabOrganizationController::Origin::kOrganizer);
    Report(moved ? Event::Kind::kMoved : Event::Kind::kDropped, request,
           TargetName(request));
    return;
  }
  if (task_name) {
    CreateTask(request, *task_name);
    return;
  }
  if (!PrepareNaming(request, {})) {
    CreateTask(request, l10n_util::GetStringUTF16(IDS_UW_UNTITLED_TASK));
    return;
  }
  Report(Event::Kind::kNaming, request,
         base::UTF8ToUTF16(request.naming.model));
  Send(std::move(request), &AutomaticOrganizer::OnName);
}

void AutomaticOrganizer::OnName(uint64_t request_id,
                                RemoteModelClient::Outcome outcome) {
  std::optional<Request> request = Take(request_id);
  if (!request) {
    return;
  }
  std::optional<std::u16string> name =
      outcome.ok ? ParseNamingResponse(outcome.body) : std::nullopt;
  request->naming.took = base::TimeTicks::Now() - request->naming.sent_at;
  request->naming.answer = std::move(outcome);
  LogOrganizerExchange("name", request->naming,
                       name ? "named" : "no usable name");
  if (request->names_task) {
    if (name && Current(*request) &&
        (!Named(request->task) || Generated(request->task))) {
      GiveName(request->task, *name);
    }
    return;
  }
  if (!Current(*request)) {
    Report(Event::Kind::kDropped, *request);
    return;
  }
  CreateTask(*request,
             name.value_or(l10n_util::GetStringUTF16(IDS_UW_UNTITLED_TASK)));
}

void AutomaticOrganizer::CreateTask(Request& request, std::u16string title) {
  const std::optional<tab_groups::TabGroupId> group =
      controller_->MoveToNewTask({request.page}, title,
                                 TabOrganizationController::Origin::kOrganizer);
  if (group) {
    generated_names_[group->ToString()] = title;
  }
  Report(group ? Event::Kind::kCreated : Event::Kind::kDropped, request,
         std::move(title));
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

bool AutomaticOrganizer::Named(const std::string& task) const {
  const std::u16string title = controller_->TaskTitle(task);
  return !title.empty() &&
         title != l10n_util::GetStringUTF16(IDS_UW_UNTITLED_TASK);
}

bool AutomaticOrganizer::Generated(const std::string& task) const {
  const auto generated = generated_names_.find(task);
  return generated != generated_names_.end() && Named(task) &&
         generated->second == controller_->TaskTitle(task);
}

void AutomaticOrganizer::GiveName(const std::string& task,
                                  const std::u16string& name) {
  controller_->NameTask(task, name);
  generated_names_[task] = name;
}

std::optional<AutomaticOrganizer::Request> AutomaticOrganizer::Take(
    uint64_t request_id) {
  const auto it = requests_.find(request_id);
  if (it == requests_.end()) {
    return std::nullopt;
  }
  Request request = std::move(it->second);
  requests_.erase(it);
  return request;
}

void AutomaticOrganizer::CancelRequestsFor(const PageId& page) {
  std::erase_if(requests_, [&page](const auto& entry) {
    return entry.second.page == page;
  });
}

std::unique_ptr<RemoteModelClient> AutomaticOrganizer::Client(
    const GURL& url,
    const std::string& key_account,
    std::string body) {
  std::string key;
  if (!key_account.empty()) {
    std::optional<std::string> stored = ApiKeyStore().GetKey(key_account);
    if (!stored || stored->empty()) {
      return nullptr;
    }
    key = std::move(*stored);
  }
  return std::make_unique<RemoteModelClient>(
      profile()
          ->GetDefaultStoragePartition()
          ->GetURLLoaderFactoryForBrowserProcess(),
      url, std::move(key), std::move(body), kProviderTimeout);
}

}  // namespace uw
