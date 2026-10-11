// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/browser/tab_organization/automatic_organizer.h"

#include <utility>

#include "base/functional/bind.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/time/time.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/grit/generated_resources.h"
#include "components/content_extraction/content/browser/inner_text.h"
#include "components/prefs/pref_service.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/storage_partition.h"
#include "content/public/browser/web_contents.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"
#include "ui/base/l10n/l10n_util.h"
#include "uw/browser/tab_organization/ai_connection.h"
#include "uw/browser/tab_organization/api_key_store.h"
#include "uw/browser/tab_organization/tab_organization_controller.h"

namespace uw {
namespace {

constexpr base::TimeDelta kDebounce = base::Milliseconds(750);
constexpr base::TimeDelta kProviderTimeout = base::Seconds(10);
constexpr char kUntitledTask[] = "Untitled task";

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

base::CallbackListSubscription AutomaticOrganizer::ObserveNotices(
    NoticeCallback callback) {
  return notices_.Add(std::move(callback));
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
  const bool enabled = Enabled();
  bool due = false;
  for (const Page& page : tree.pages()) {
    const tabs::TabInterface* tab = controller_->TabFor(page.id);
    content::WebContents* contents = tab ? tab->GetContents() : nullptr;
    if (!contents) {
      continue;
    }
    Activity& activity = pages_[page.id];
    const bool first_sight = activity.revision == 0;
    if (!first_sight && activity.url == contents->GetLastCommittedURL() &&
        activity.title == contents->GetTitle()) {
      continue;
    }
    activity.url = contents->GetLastCommittedURL();
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
  if (!Enabled() || !page || activity == pages_.end() ||
      activity->second.revision != revision) {
    return;
  }
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
      parent == pages_.end()
          ? "no source tab"
          : "opened from \"" + base::UTF16ToUTF8(parent->second.title) + "\"";
  request.context.excerpt = std::move(excerpt);

  std::vector<std::pair<std::string, std::string>> tasks;
  for (const std::string& task : controller_->tree().Tasks()) {
    const std::u16string title = controller_->TaskTitle(task);
    tasks.emplace_back(
        task, title.empty() ? kUntitledTask : base::UTF16ToUTF8(title));
  }
  std::optional<std::string> body = BuildDecisionRequestBody(
      request.format, decides ? decision.model : language.model,
      request.context, tasks);
  if (!body) {
    return;
  }
  request.client = Client(
      decides ? DecisionUrl(decision) : ChatCompletionsUrl(language.endpoint),
      decides ? decision.key_account : language.key_account, std::move(*body));
  if (!request.client) {
    return;
  }
  CancelRequestsFor(id);
  const uint64_t request_id = next_request_++;
  RemoteModelClient* client = request.client.get();
  requests_.emplace(request_id, std::move(request));
  client->Send(base::BindOnce(&AutomaticOrganizer::OnDecision,
                              weak_factory_.GetWeakPtr(), request_id));
}

void AutomaticOrganizer::OnDecision(uint64_t request_id,
                                    RemoteModelClient::Outcome outcome) {
  std::optional<Request> request = Take(request_id);
  if (!request || !outcome.ok || !Current(*request)) {
    return;
  }
  const std::optional<ClassificationAnswer> answer =
      ParseDecisionResponse(request->format, outcome.body);
  if (!answer || answer->confidence < kMinConfidence ||
      answer->choice == kScratchpadOption) {
    return;
  }
  if (answer->choice != kNewTaskOption) {
    const std::optional<tab_groups::TabGroupId> group =
        controller_->GroupFor(answer->choice);
    if (group && answer->choice != request->task &&
        controller_->MoveToTask(
            {request->page}, *group,
            TabOrganizationController::Origin::kOrganizer)) {
      notices_.Notify(Notice{controller_->TaskTitle(answer->choice), false});
    }
    return;
  }
  if (answer->task_name) {
    CreateTask(*request, *answer->task_name);
    return;
  }
  const AiConnectionConfig language =
      ReadAiConnectionConfig(profile()->GetPrefs());
  std::optional<std::string> body =
      IsComplete(language)
          ? BuildNamingRequestBody(language.model, request->context)
          : std::nullopt;
  std::unique_ptr<RemoteModelClient> naming =
      body ? Client(ChatCompletionsUrl(language.endpoint), language.key_account,
                    std::move(*body))
           : nullptr;
  if (!naming) {
    CreateTask(*request, l10n_util::GetStringUTF16(IDS_UW_UNTITLED_TASK));
    return;
  }
  const uint64_t naming_id = next_request_++;
  RemoteModelClient* client = naming.get();
  request->client = std::move(naming);
  requests_.emplace(naming_id, std::move(*request));
  client->Send(base::BindOnce(&AutomaticOrganizer::OnName,
                              weak_factory_.GetWeakPtr(), naming_id));
}

void AutomaticOrganizer::OnName(uint64_t request_id,
                                RemoteModelClient::Outcome outcome) {
  std::optional<Request> request = Take(request_id);
  if (!request || !Current(*request)) {
    return;
  }
  std::optional<std::u16string> name =
      outcome.ok ? ParseNamingResponse(outcome.body) : std::nullopt;
  CreateTask(*request,
             name.value_or(l10n_util::GetStringUTF16(IDS_UW_UNTITLED_TASK)));
}

void AutomaticOrganizer::CreateTask(const Request& request,
                                    std::u16string title) {
  if (controller_->MoveToNewTask(
          {request.page}, title,
          TabOrganizationController::Origin::kOrganizer)) {
    notices_.Notify(Notice{std::move(title), true});
  }
}

bool AutomaticOrganizer::Current(const Request& request) const {
  const Page* page = controller_->tree().Find(request.page);
  const auto activity = pages_.find(request.page);
  return Enabled() && page && activity != pages_.end() &&
         activity->second.revision == request.revision &&
         page->task == request.task && page->parent == request.parent;
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
