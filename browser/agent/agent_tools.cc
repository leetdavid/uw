// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/browser/agent/agent_tools.h"

#include <algorithm>
#include <optional>
#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/memory/raw_ref.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/to_string.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/actor/actor_keyed_service.h"
#include "chrome/browser/actor/actor_task.h"
#include "chrome/browser/actor/actor_task_delegate.h"
#include "chrome/browser/actor/actor_task_metadata.h"
#include "chrome/browser/actor/enterprise_policy_checker.h"
#include "chrome/browser/actor/tab_observation_strategy.h"
#include "chrome/browser/actor/tools/click_tool_request.h"
#include "chrome/browser/actor/tools/navigate_tool_request.h"
#include "chrome/browser/actor/tools/type_tool_request.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/navigator/browser_navigator.h"
#include "chrome/browser/ui/navigator/browser_navigator_params.h"
#include "chrome/common/actor.mojom.h"
#include "chrome/common/actor/action_result.h"
#include "chrome/common/actor_webui.mojom.h"
#include "components/actor/core/task_source_info.h"
#include "components/actor/public/mojom/actor_types.mojom.h"
#include "content/public/browser/web_contents.h"
#include "ui/base/page_transition_types.h"
#include "ui/base/window_open_disposition.h"
#include "url/gurl.h"
#include "url/origin.h"
#include "url/url_constants.h"
#include "uw/browser/terminal/terminal_tab.h"

namespace uw {
namespace {

// About 5,000 words: most of a long article, and still a modest part of a
// model's context.
constexpr size_t kMaxPageText = 30000;

base::DictValue Error(std::string_view message) {
  return base::DictValue().Set("error", message);
}

std::string Refusal(const actor::mojom::ActionResult& result) {
  if (result.code ==
      actor::mojom::ActionResultCode::kActionsBlockedSafeBrowsingDisabled) {
    return "Chromium's agent engine acts only while Safe Browsing is on. The "
           "user can turn it on in Settings, under Privacy and security.";
  }
  return result.message.empty()
             ? base::StrCat({"The agent engine refused: ",
                             base::ToString(result.code), "."})
             : result.message;
}

}  // namespace

// Answers what the engine asks the user. uw asks only about a site Chromium
// marks as sensitive, as an approval in the chat. The rest get the answers
// the engine gives itself when no one is there to ask.
class AgentTools::EngineDelegate : public actor::ActorTaskDelegate {
 public:
  explicit EngineDelegate(AgentTools& tools) : tools_(tools) {}

  base::WeakPtr<EngineDelegate> GetWeakPtr() {
    return weak_factory_.GetWeakPtr();
  }

  // actor::ActorTaskDelegate:
  void OnTabAddedToTask(actor::TaskId task_id,
                        const tabs::TabInterface::Handle& tab) override {}
  void OnTaskTabsVisibilityChanged(actor::TaskId task_id,
                                   bool has_visible_tab) override {}
  void RequestToShowCredentialSelectionDialog(
      actor::TaskId task_id,
      const base::flat_map<std::string, gfx::Image>& icons,
      const std::vector<actor_login::Credential>& credentials,
      CredentialSelectedCallback callback) override {
    std::move(callback).Run(
        actor::webui::mojom::SelectCredentialDialogResponse::New());
  }
  void RequestToShowUserConfirmationDialog(
      actor::TaskId task_id,
      const url::Origin& site,
      bool for_blocklisted_origin,
      UserConfirmationDialogCallback callback) override {
    tools_->AskAboutSite(
        site, for_blocklisted_origin,
        base::BindOnce(
            [](UserConfirmationDialogCallback callback, bool approved) {
              auto response =
                  actor::webui::mojom::UserConfirmationDialogResponse::New();
              response->result = actor::webui::mojom::
                  ConfirmationRequestResult::NewPermissionGranted(approved);
              std::move(callback).Run(std::move(response));
            },
            std::move(callback)));
  }
  void RequestToConfirmNavigation(
      actor::TaskId task_id,
      const url::Origin& site,
      NavigationConfirmationCallback callback) override {
    auto response = actor::webui::mojom::NavigationConfirmationResponse::New();
    response->result =
        actor::webui::mojom::ConfirmationRequestResult::NewPermissionGranted(
            false);
    std::move(callback).Run(std::move(response));
  }
  void RequestToShowAutofillSuggestionsDialog(
      actor::TaskId task_id,
      std::vector<autofill::ActorFormFillingRequest> requests,
      base::WeakPtr<actor::AutofillSelectionDialogEventHandler> event_handler,
      AutofillSuggestionSelectedCallback callback) override {
    std::move(callback).Run(
        actor::webui::mojom::SelectAutofillSuggestionsDialogResponse::New(
            task_id.value(),
            actor::webui::mojom::SelectAutofillSuggestionsDialogResult::
                NewErrorReason(actor::webui::mojom::
                                   SelectAutofillSuggestionsDialogErrorReason::
                                       kNoActorTaskDelegate)));
  }
  void RequestToShowGmailOtpOptInDialog(
      actor::TaskId task_id,
      GmailOtpOptInCallback callback) override {
    std::move(callback).Run(
        actor::webui::mojom::GmailOtpOptInResult::NewErrorReason(
            actor::webui::mojom::GmailOtpErrorReason::
                kRequestPromiseNoSubscriber));
  }
  void RequestToShowGmailOtpConfirmationDialog(
      actor::TaskId task_id,
      const std::string& verification_code,
      GmailOtpConfirmationCallback callback) override {
    std::move(callback).Run(
        actor::webui::mojom::GmailOtpConfirmationResult::NewErrorReason(
            actor::webui::mojom::GmailOtpErrorReason::
                kRequestPromiseNoSubscriber));
  }

 private:
  const raw_ref<AgentTools> tools_;
  base::WeakPtrFactory<EngineDelegate> weak_factory_{this};
};

AgentTools::AgentTools(tabs::TabInterface& chat)
    : chat_(chat.GetHandle()),
      delegate_(std::make_unique<EngineDelegate>(*this)) {
  actor::ActorKeyedService* engine = actor::ActorKeyedService::Get(
      chat.GetBrowserWindowInterface()->GetProfile());
  if (!engine) {
    return;
  }
  engine_ = engine->GetWeakPtr();
  task_ = engine->CreateTaskWithOptions(
      actor::TaskSourceInfo(actor::TaskSourceInfo::Client::kExperimentalActor,
                            std::nullopt),
      actor::GetNullEnterprisePolicyChecker(), /*options=*/nullptr,
      delegate_->GetWeakPtr());
}

AgentTools::~AgentTools() {
  if (engine_ && engine_->GetTask(task_)) {
    engine_->StopTask(task_, actor::ActorTask::StoppedReason::kTaskComplete);
  }
}

void AgentTools::Call(std::string_view tool,
                      const base::DictValue& args,
                      Done done) {
  if (tool == "run") {
    Run(args, std::move(done));
    return;
  }
  if (tool == "screen") {
    Screen(args, std::move(done));
    return;
  }
  if (tool == "keys") {
    Keys(args, std::move(done));
    return;
  }
  if (tool == kTriggerTool && start_trigger_) {
    Trigger(args, std::move(done));
    return;
  }
  if (tool == kStopTriggerTool && stop_trigger_) {
    std::move(done).Run(stop_trigger_.Run());
    return;
  }
  if (!engine_ || !engine_->GetTask(task_)) {
    std::move(done).Run(Error("Chromium's agent engine is not running."));
    return;
  }
  acting_tab_ = tool == "open" ? next_tab_ : args.FindInt("tab").value_or(0);
  if (tool == "open") {
    Open(args, std::move(done));
  } else if (tool == "navigate") {
    Navigate(args, std::move(done));
  } else if (tool == "read") {
    Read(args, std::move(done));
  } else if (tool == "click") {
    Click(args, std::move(done));
  } else if (tool == "type") {
    Type(args, std::move(done));
  } else {
    std::move(done).Run(
        Error(base::StrCat({"There is no tool called ", tool, "."})));
  }
}

std::optional<AgentTools::Approval> AgentTools::ApprovalFor(
    std::string_view tool,
    const base::DictValue& args,
    bool every_step) {
  if (IsTerminalTool(tool)) {
    return TerminalApprovalFor(tool, args, every_step);
  }
  if (tool != "click" && tool != "type") {
    return std::nullopt;
  }
  std::string error;
  Tab* tab = WebTabFrom(args, error);
  const PageElement* element = tab ? ElementFrom(*tab, args, error) : nullptr;
  if (!element) {
    // The step fails on its own.
    return std::nullopt;
  }
  const bool enter = tool == "type" && args.FindBool("enter").value_or(false);
  Approval approval;
  if (tool == "click" && element->submits) {
    approval.reason = "It submits a form.";
  } else if (tool == "type" && !element->fills.empty()) {
    approval.reason = base::StrCat({"It fills in ", element->fills, "."});
  } else if (enter && element->enter_submits) {
    approval.reason = "Pressing Enter submits a form.";
  } else if (args.FindBool("changes").value_or(false)) {
    approval.reason = "The agent says this changes something.";
  } else if (every_step) {
    approval.reason = "Ask first is on.";
  } else {
    return std::nullopt;
  }
  approval.tab = *args.FindInt("tab");
  approval.site =
      tab->handle.Get()->GetContents()->GetLastCommittedURL().host();
  const std::string* text = args.FindString("text");
  if (tool == "click") {
    approval.action = base::StrCat({"Click ", element->name});
  } else if (text && element->fills.empty()) {
    approval.action =
        base::StrCat({"Type \"", *text, "\" into ", element->name});
  } else {
    approval.action = base::StrCat({"Type into ", element->name});
  }
  if (enter) {
    base::StrAppend(&approval.action, {" and press Enter"});
  }
  return approval;
}

void AgentTools::AskAboutSite(const url::Origin& site,
                              bool sensitive,
                              base::OnceCallback<void(bool)> done) {
  if (!confirm_site_) {
    std::move(done).Run(false);
    return;
  }
  Approval approval;
  approval.tab = acting_tab_;
  approval.site = site.host();
  approval.action = base::StrCat({"Let the agent act on ", site.host()});
  approval.reason = sensitive ? "Chromium marks this site as sensitive."
                              : "The agent has not used this site before.";
  confirm_site_.Run(std::move(approval), std::move(done));
}

std::map<int, tabs::TabInterface*> AgentTools::OpenTabs() const {
  std::map<int, tabs::TabInterface*> open;
  for (const auto& [number, tab] : tabs_) {
    if (tabs::TabInterface* opened = tab.handle.Get()) {
      open[number] = opened;
    }
  }
  return open;
}

bool AgentTools::IsTerminalTab(int number) const {
  const auto tab = tabs_.find(number);
  return tab != tabs_.end() && tab->second.terminal;
}

void AgentTools::Adopt(int number, tabs::TabInterface& tab) {
  if (!engine_ || !engine_->GetTask(task_) || tabs_.contains(number) ||
      IsTerminalUrl(tab.GetContents()->GetLastCommittedURL()) ||
      IsTerminalUrl(tab.GetContents()->GetVisibleURL())) {
    return;
  }
  tabs_[number].handle = tab.GetHandle();
  next_tab_ = std::max(next_tab_, number + 1);
  engine_->GetTask(task_)->AddTab(tab.GetHandle(),
                                  /*stop_task_on_detach=*/false,
                                  base::DoNothing());
}

void AgentTools::Open(const base::DictValue& args, Done done) {
  const std::string* spec = args.FindString("url");
  const GURL url(spec ? *spec : std::string());
  if (!url.SchemeIsHTTPOrHTTPS()) {
    std::move(done).Run(Error("open needs an http or https address."));
    return;
  }
  tabs::TabInterface* chat = chat_.Get();
  if (!chat) {
    std::move(done).Run(Error("The chat's tab is closed."));
    return;
  }
  // Opened as a link from the chat's tab, the new tab nests under it. It
  // starts blank so that the engine, and its site checks, load the page.
  NavigateParams params(chat->GetBrowserWindowInterface(),
                        GURL(url::kAboutBlankURL), ui::PAGE_TRANSITION_LINK);
  params.disposition = WindowOpenDisposition::NEW_BACKGROUND_TAB;
  params.source_contents = chat->GetContents();
  ::Navigate(&params);
  tabs::TabInterface* tab = params.navigated_or_inserted_contents
                                ? tabs::TabInterface::MaybeGetFromContents(
                                      params.navigated_or_inserted_contents)
                                : nullptr;
  if (!tab) {
    std::move(done).Run(Error("The tab could not be opened."));
    return;
  }
  const int id = next_tab_++;
  tabs_[id].handle = tab->GetHandle();
  engine_->GetTask(task_)->AddTab(
      tab->GetHandle(), /*stop_task_on_detach=*/false,
      base::BindOnce(
          [](base::WeakPtr<AgentTools> self, int id, GURL url, Done done,
             actor::mojom::ActionResultPtr added) {
            if (!self) {
              return;
            }
            if (added->code != actor::mojom::ActionResultCode::kOk) {
              std::move(done).Run(Error(Refusal(*added)));
              return;
            }
            self->Act(std::make_unique<actor::NavigateToolRequest>(
                          self->tabs_[id].handle, std::move(url)),
                      base::DictValue().Set("tab", id), std::move(done));
          },
          weak_factory_.GetWeakPtr(), id, url, std::move(done)));
}

void AgentTools::Navigate(const base::DictValue& args, Done done) {
  std::string error;
  Tab* tab = WebTabFrom(args, error);
  const std::string* spec = args.FindString("url");
  const GURL url(spec ? *spec : std::string());
  if (tab && !url.SchemeIsHTTPOrHTTPS()) {
    error = "navigate needs an http or https address.";
  }
  if (!error.empty()) {
    std::move(done).Run(Error(error));
    return;
  }
  Act(std::make_unique<actor::NavigateToolRequest>(tab->handle, url),
      base::DictValue(), std::move(done));
}

void AgentTools::Read(const base::DictValue& args, Done done) {
  std::string error;
  Tab* tab = WebTabFrom(args, error);
  if (!tab) {
    std::move(done).Run(Error(error));
    return;
  }
  const int id = *args.FindInt("tab");
  engine_->RequestTabObservation(
      *tab->handle.Get(), task_, std::nullopt,
      base::BindOnce(
          [](base::WeakPtr<AgentTools> self, int id, Done done,
             actor::ActorKeyedService::TabObservationResult seen) {
            if (!self) {
              return;
            }
            if (!seen.has_value()) {
              std::move(done).Run(Error(seen.error().message));
              return;
            }
            const auto& content = seen.value()->annotated_page_content_result;
            if (!content.has_value()) {
              std::move(done).Run(Error(content.error()));
              return;
            }
            const auto tab = self->tabs_.find(id);
            content::WebContents* contents =
                tab == self->tabs_.end() || !tab->second.handle.Get()
                    ? nullptr
                    : tab->second.handle.Get()->GetContents();
            if (!contents) {
              std::move(done).Run(Error("That tab is closed."));
              return;
            }
            tab->second.page = DescribePage(content->proto, kMaxPageText);
            std::move(done).Run(
                base::DictValue()
                    .Set("title", base::UTF16ToUTF8(contents->GetTitle()))
                    .Set("url", contents->GetLastCommittedURL().spec())
                    .Set("text", tab->second.page.text));
          },
          weak_factory_.GetWeakPtr(), id, std::move(done)));
}

void AgentTools::Click(const base::DictValue& args, Done done) {
  std::string error;
  Tab* tab = WebTabFrom(args, error);
  const PageElement* element = tab ? ElementFrom(*tab, args, error) : nullptr;
  if (!element) {
    std::move(done).Run(Error(error));
    return;
  }
  Act(std::make_unique<actor::ClickToolRequest>(
          tab->handle, actor::DomNode{element->node_id, element->document},
          actor::mojom::ClickType::kLeft, actor::mojom::ClickCount::kSingle),
      base::DictValue().Set("element", element->name), std::move(done));
}

void AgentTools::Type(const base::DictValue& args, Done done) {
  std::string error;
  Tab* tab = WebTabFrom(args, error);
  const PageElement* element = tab ? ElementFrom(*tab, args, error) : nullptr;
  const std::string* text = args.FindString("text");
  if (element && !text) {
    error = "type needs the text to type.";
  }
  if (!error.empty()) {
    std::move(done).Run(Error(error));
    return;
  }
  Act(std::make_unique<actor::TypeToolRequest>(
          tab->handle, actor::DomNode{element->node_id, element->document},
          *text, args.FindBool("enter").value_or(false),
          actor::TypeToolRequest::Mode::kReplace),
      base::DictValue().Set("element", element->name), std::move(done));
}

AgentTools::Tab* AgentTools::TabFrom(const base::DictValue& args,
                                     std::string& error) {
  const std::optional<int> id = args.FindInt("tab");
  const auto tab = id ? tabs_.find(*id) : tabs_.end();
  if (tab == tabs_.end()) {
    error = "That is not one of the agent's tabs. Open one first.";
    return nullptr;
  }
  if (!tab->second.handle.Get()) {
    error = base::StrCat({"Tab ", base::NumberToString(*id), " is closed."});
    return nullptr;
  }
  return &tab->second;
}

AgentTools::Tab* AgentTools::WebTabFrom(const base::DictValue& args,
                                        std::string& error) {
  Tab* tab = TabFrom(args, error);
  if (tab && tab->terminal) {
    error = base::StrCat({"Tab ", base::NumberToString(*args.FindInt("tab")),
                          " is a terminal. Use run, screen, or keys there."});
    return nullptr;
  }
  return tab;
}

const PageElement* AgentTools::ElementFrom(const Tab& tab,
                                           const base::DictValue& args,
                                           std::string& error) {
  const std::optional<int> number = args.FindInt("element");
  if (!number || *number < 1 ||
      static_cast<size_t>(*number) > tab.page.elements.size()) {
    error =
        "That element is not on the tab's latest reading. Read the tab "
        "again.";
    return nullptr;
  }
  return &tab.page.elements[*number - 1];
}

void AgentTools::Act(std::unique_ptr<actor::ToolRequest> action,
                     base::DictValue result,
                     Done done) {
  std::vector<std::unique_ptr<actor::ToolRequest>> actions;
  actions.push_back(std::move(action));
  engine_->PerformActions(
      task_, std::move(actions), actor::ActorTaskMetadata(),
      base::BindOnce(
          [](base::DictValue result, Done done,
             std::vector<actor::ActionResultWithLatencyInfo> results,
             actor::TabObservationStrategy) {
            if (results.empty()) {
              std::move(done).Run(Error("The agent engine did nothing."));
              return;
            }
            for (const actor::ActionResultWithLatencyInfo& ended : results) {
              if (ended.result->code != actor::mojom::ActionResultCode::kOk) {
                std::move(done).Run(Error(Refusal(*ended.result)));
                return;
              }
            }
            std::move(done).Run(std::move(result));
          },
          std::move(result), std::move(done)));
}

}  // namespace uw
