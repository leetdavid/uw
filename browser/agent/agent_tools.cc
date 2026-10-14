// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/browser/agent/agent_tools.h"

#include <optional>
#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/to_string.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/actor/actor_keyed_service.h"
#include "chrome/browser/actor/actor_task.h"
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
#include "components/actor/core/task_source_info.h"
#include "components/actor/public/mojom/actor_types.mojom.h"
#include "content/public/browser/web_contents.h"
#include "ui/base/page_transition_types.h"
#include "ui/base/window_open_disposition.h"
#include "url/gurl.h"
#include "url/url_constants.h"

namespace uw {
namespace {

// About 5,000 words: most of a long article, and still a modest part of a
// model's context.
constexpr size_t kMaxPageText = 30000;

base::DictValue Error(std::string_view message) {
  return base::DictValue().Set("error", message);
}

std::string Refusal(const actor::mojom::ActionResult& result) {
  return result.message.empty()
             ? base::StrCat({"The agent engine refused: ",
                             base::ToString(result.code), "."})
             : result.message;
}

}  // namespace

AgentTools::AgentTools(tabs::TabInterface& chat) : chat_(chat.GetHandle()) {
  actor::ActorKeyedService* engine = actor::ActorKeyedService::Get(
      chat.GetBrowserWindowInterface()->GetProfile());
  if (!engine) {
    return;
  }
  engine_ = engine->GetWeakPtr();
  task_ = engine->CreateTask(
      actor::TaskSourceInfo(actor::TaskSourceInfo::Client::kExperimentalActor,
                            std::nullopt),
      actor::GetNullEnterprisePolicyChecker());
}

AgentTools::~AgentTools() {
  if (engine_ && engine_->GetTask(task_)) {
    engine_->StopTask(task_, actor::ActorTask::StoppedReason::kTaskComplete);
  }
}

void AgentTools::Call(std::string_view tool,
                      const base::DictValue& args,
                      Done done) {
  if (!engine_ || !engine_->GetTask(task_)) {
    std::move(done).Run(Error("Chromium's agent engine is not running."));
    return;
  }
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
  Tab* tab = TabFrom(args, error);
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
  Tab* tab = TabFrom(args, error);
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
  Tab* tab = TabFrom(args, error);
  const PageElement* element = tab ? ElementFrom(*tab, args, error) : nullptr;
  if (!element) {
    std::move(done).Run(Error(error));
    return;
  }
  Act(std::make_unique<actor::ClickToolRequest>(
          tab->handle, actor::DomNode{element->node_id, element->document},
          actor::mojom::ClickType::kLeft, actor::mojom::ClickCount::kSingle),
      base::DictValue(), std::move(done));
}

void AgentTools::Type(const base::DictValue& args, Done done) {
  std::string error;
  Tab* tab = TabFrom(args, error);
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
      base::DictValue(), std::move(done));
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
