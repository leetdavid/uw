// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/browser/ui/webui/uw_chat/uw_chat_handler.h"

#include <set>
#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "base/json/json_writer.h"
#include "base/no_destructor.h"
#include "chrome/browser/profiles/profile.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/web_contents.h"
#include "uw/browser/tab_organization/ai_connection.h"
#include "uw/browser/tab_organization/api_key_store.h"
#include "uw/browser/tab_organization/tab_organization_controller.h"
#include "uw/components/agent/agent_context.h"

namespace uw {
namespace {

// What the model is told about a step that was under way when the chat
// stopped.
constexpr char kCutShort[] =
    "The chat stopped before this step finished, so it may or may not have "
    "happened.";

// The chats open in a page, so that a second page for the same chat, such
// as a duplicated tab, starts a chat of its own.
std::set<std::string>& OpenChats() {
  static base::NoDestructor<std::set<std::string>> open;
  return *open;
}

mojom::StepPtr Failed(std::string text) {
  auto step = mojom::Step::New();
  step->kind = mojom::StepKind::kFailed;
  step->text = std::move(text);
  return step;
}

mojom::StepKind KindOf(AgentLoop::Step::Kind kind) {
  switch (kind) {
    case AgentLoop::Step::Kind::kTool:
      return mojom::StepKind::kTool;
    case AgentLoop::Step::Kind::kAnswer:
      return mojom::StepKind::kAnswer;
    case AgentLoop::Step::Kind::kFailed:
      return mojom::StepKind::kFailed;
  }
}

std::string Json(const base::DictValue* value) {
  return value ? base::WriteJson(*value).value_or("{}") : "{}";
}

}  // namespace

UwChatHandler::UwChatHandler(content::WebContents* contents,
                             mojo::PendingRemote<mojom::ChatPage> page,
                             mojo::PendingReceiver<mojom::ChatHandler> receiver)
    : contents_(contents),
      page_(std::move(page)),
      receiver_(this, std::move(receiver)) {
  const std::string_view path = contents_->GetLastCommittedURL().path();
  id_ = path.empty() ? std::string() : std::string(path.substr(1));
  if (!ChatFile::IsChatId(id_) || OpenChats().contains(id_)) {
    id_ = ChatFile::NewChatId();
  }
  OpenChats().insert(id_);
  Profile* profile =
      Profile::FromBrowserContext(contents_->GetBrowserContext());
  if (profile->IsOffTheRecord()) {
    OnLoaded(std::nullopt);
    return;
  }
  file_ = std::make_unique<ChatFile>(profile->GetPath(), id_);
  file_->Load(
      base::BindOnce(&UwChatHandler::OnLoaded, weak_factory_.GetWeakPtr()));
}

UwChatHandler::~UwChatHandler() {
  OpenChats().erase(id_);
}

void UwChatHandler::Send(const std::string& text) {
  if (!loaded_ || Working() || (!agent_ && !Start())) {
    return;
  }
  if (title_.empty()) {
    title_ = text;
  }
  Log(base::DictValue().Set("kind", "user").Set("text", text));
  agent_->Send(text);
  Save();
}

void UwChatHandler::Resume() {
  const base::DictValue* last =
      log_.empty() ? nullptr : log_.back().GetIfDict();
  const std::string* kind = last ? last->FindString("kind") : nullptr;
  if (!loaded_ || Working() || !kind || *kind != "stopped" ||
      (!agent_ && !Start())) {
    return;
  }
  Log(base::DictValue().Set("kind", "resumed"));
  agent_->Resume();
  Save();
}

void UwChatHandler::OnLoaded(std::optional<base::DictValue> saved) {
  loaded_ = true;
  if (saved) {
    if (const std::string* title = saved->FindString("title")) {
      title_ = *title;
    }
    if (const base::ListValue* log = saved->FindList("log")) {
      log_ = log->Clone();
    }
    if (const base::DictValue* context = saved->FindDict("context")) {
      saved_context_ = AgentContext::FromValue(*context);
    }
    if (const base::ListValue* tabs = saved->FindList("tabs")) {
      for (const base::Value& entry : *tabs) {
        const base::DictValue* tab = entry.GetIfDict();
        const std::optional<int> number =
            tab ? tab->FindInt("number") : std::nullopt;
        const std::string* page = tab ? tab->FindString("page") : nullptr;
        if (number && page) {
          saved_tabs_[*number] = *page;
        }
      }
    }
    if (saved->FindBool("running").value_or(false)) {
      if (saved_context_) {
        saved_context_->CloseOpenCalls(kCutShort);
      }
      Log(base::DictValue().Set("kind", "stopped"));
      Save();
    }
  }
  std::vector<mojom::StepPtr> lines;
  for (const base::Value& entry : log_) {
    if (entry.is_dict()) {
      lines.push_back(LineFor(entry.GetDict()));
    }
  }
  page_->OnOpened(id_, title_, std::move(lines));
}

bool UwChatHandler::Start() {
  tabs::TabInterface* tab = tabs::TabInterface::MaybeGetFromContents(contents_);
  if (!tab) {
    page_->OnStep(Failed("A chat works only in a tab."));
    return false;
  }
  const AiConnectionConfig config = ReadAgentModelConfig(
      Profile::FromBrowserContext(contents_->GetBrowserContext())->GetPrefs());
  if (!IsComplete(config)) {
    page_->OnStep(Failed("Set an agent model on uw's settings page first."));
    return false;
  }
  AgentModel model;
  model.url = ChatCompletionsUrl(config.endpoint);
  model.model = config.model;
  if (!config.key_account.empty()) {
    model.api_key =
        ApiKeyStore().GetKey(config.key_account).value_or(std::string());
  }
  agent_ = std::make_unique<AgentLoop>(
      *tab, std::move(model),
      base::BindRepeating(&UwChatHandler::OnStep, base::Unretained(this)),
      std::move(saved_context_),
      base::BindRepeating(&UwChatHandler::Save, base::Unretained(this)));
  if (TabOrganizationController* tree =
          TabOrganizationController::From(tab->GetBrowserWindowInterface())) {
    for (const auto& [number, page] : saved_tabs_) {
      if (tabs::TabInterface* restored = tree->TabFor(PageId(page))) {
        agent_->tools().Adopt(number, *restored);
      }
    }
  }
  saved_tabs_.clear();
  return true;
}

void UwChatHandler::OnStep(const AgentLoop::Step& step) {
  switch (step.kind) {
    case AgentLoop::Step::Kind::kTool:
      Log(base::DictValue().Set("kind", "step").Set("step", step.number));
      break;
    case AgentLoop::Step::Kind::kAnswer:
      Log(base::DictValue().Set("kind", "answer").Set("text", step.text));
      break;
    case AgentLoop::Step::Kind::kFailed:
      Log(base::DictValue().Set("kind", "failed").Set("text", step.text));
      break;
  }
  Save();
  auto sent = mojom::Step::New();
  sent->kind = KindOf(step.kind);
  sent->tool = step.tool;
  sent->args = Json(&step.args);
  sent->result = Json(&step.result);
  sent->text = step.text;
  page_->OnStep(std::move(sent));
}

bool UwChatHandler::Working() const {
  return agent_ && agent_->working();
}

mojom::StepPtr UwChatHandler::LineFor(const base::DictValue& entry) const {
  auto line = mojom::Step::New();
  const std::string* kind = entry.FindString("kind");
  const std::string* text = entry.FindString("text");
  line->text = text ? *text : std::string();
  line->kind = mojom::StepKind::kFailed;
  if (!kind) {
    return line;
  }
  if (*kind == "step") {
    const AgentContext* context =
        agent_ ? &agent_->context() : saved_context_.get();
    const base::DictValue step =
        context ? context->Recall(base::DictValue().Set(
                      "step", entry.FindInt("step").value_or(0)))
                : base::DictValue();
    const std::string* tool = step.FindString("tool");
    line->kind = mojom::StepKind::kTool;
    line->tool = tool ? *tool : std::string();
    line->args = Json(step.FindDict("args"));
    line->result = Json(step.FindDict("result"));
  } else if (*kind == "user") {
    line->kind = mojom::StepKind::kUser;
  } else if (*kind == "answer") {
    line->kind = mojom::StepKind::kAnswer;
  } else if (*kind == "stopped") {
    line->kind = mojom::StepKind::kStopped;
  } else if (*kind == "resumed") {
    line->kind = mojom::StepKind::kResumed;
  }
  return line;
}

void UwChatHandler::Log(base::DictValue entry) {
  log_.Append(std::move(entry));
}

void UwChatHandler::Save() {
  const AgentContext* context =
      agent_ ? &agent_->context() : saved_context_.get();
  if (!file_ || !context) {
    return;
  }
  base::ListValue tabs;
  tabs::TabInterface* chat =
      tabs::TabInterface::MaybeGetFromContents(contents_);
  TabOrganizationController* tree =
      chat ? TabOrganizationController::From(chat->GetBrowserWindowInterface())
           : nullptr;
  if (agent_ && tree) {
    for (const auto& [number, tab] : agent_->tools().OpenTabs()) {
      if (const std::optional<PageId> page = tree->IdFor(tab)) {
        tabs.Append(
            base::DictValue().Set("number", number).Set("page", page->value()));
      }
    }
  } else {
    for (const auto& [number, page] : saved_tabs_) {
      tabs.Append(base::DictValue().Set("number", number).Set("page", page));
    }
  }
  file_->Save(base::DictValue()
                  .Set("version", 1)
                  .Set("title", title_)
                  .Set("running", Working())
                  .Set("log", log_.Clone())
                  .Set("context", context->ToValue())
                  .Set("tabs", std::move(tabs)));
}

}  // namespace uw
