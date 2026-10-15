// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/browser/ui/webui/uw_chat/uw_chat_handler.h"

#include <set>
#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "base/json/json_writer.h"
#include "base/json/values_util.h"
#include "base/no_destructor.h"
#include "base/strings/strcat.h"
#include "base/task/sequenced_task_runner.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/tab_list/tab_list_interface.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface_iterator.h"
#include "chrome/browser/ui/navigator/browser_navigator.h"
#include "chrome/browser/ui/navigator/browser_navigator_params.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/url_constants.h"
#include "ui/base/base_window.h"
#include "ui/base/page_transition_types.h"
#include "ui/base/window_open_disposition.h"
#include "url/gurl.h"
#include "uw/browser/agent/chat_tab.h"
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

mojom::ChatState StateFor(ChatIndex::State state) {
  switch (state) {
    case ChatIndex::State::kDone:
      return mojom::ChatState::kDone;
    case ChatIndex::State::kRunning:
      return mojom::ChatState::kRunning;
    case ChatIndex::State::kStopped:
      return mojom::ChatState::kStopped;
    case ChatIndex::State::kUnread:
      return mojom::ChatState::kUnread;
  }
}

GURL ChatUrl(std::string_view id) {
  return GURL(
      base::StrCat({content::kChromeUIScheme, "://", kChatHost, "/", id}));
}

// The tabs of `profile`, in every window, that show the chat `id`. A tab
// that session restore has not loaded yet counts by the address it will load.
std::vector<tabs::TabInterface*> TabsShowing(Profile* profile,
                                             std::string_view id) {
  const GURL url = ChatUrl(id);
  std::vector<tabs::TabInterface*> showing;
  for (BrowserWindowInterface* window : GetAllBrowserWindowInterfaces()) {
    if (window->GetProfile() != profile) {
      continue;
    }
    for (tabs::TabInterface* tab :
         TabListInterface::From(window)->GetAllTabs()) {
      if (tab->GetContents()->GetVisibleURL() == url) {
        showing.push_back(tab);
      }
    }
  }
  return showing;
}

}  // namespace

UwChatHandler::UwChatHandler(content::WebContents* contents,
                             mojo::PendingRemote<mojom::ChatPage> page,
                             mojo::PendingReceiver<mojom::ChatHandler> receiver)
    : content::WebContentsObserver(contents),
      contents_(contents),
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
  if (ChatIndex* index = ChatIndex::For(profile)) {
    index_observation_.Observe(index);
  }
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

void UwChatHandler::OpenChat(const std::string& id) {
  if (!ChatFile::IsChatId(id)) {
    return;
  }
  Profile* profile =
      Profile::FromBrowserContext(contents_->GetBrowserContext());
  std::vector<tabs::TabInterface*> showing = TabsShowing(profile, id);
  if (!showing.empty()) {
    BrowserWindowInterface* window = showing[0]->GetBrowserWindowInterface();
    TabListInterface::From(window)->ActivateTab(showing[0]->GetHandle());
    window->GetWindow()->Activate();
    return;
  }
  tabs::TabInterface* chat =
      tabs::TabInterface::MaybeGetFromContents(contents_);
  if (!chat) {
    return;
  }
  NavigateParams params(chat->GetBrowserWindowInterface(), ChatUrl(id),
                        ui::PAGE_TRANSITION_AUTO_TOPLEVEL);
  params.disposition = WindowOpenDisposition::NEW_FOREGROUND_TAB;
  Navigate(&params);
}

void UwChatHandler::NewChat() {
  if (tabs::TabInterface* chat =
          tabs::TabInterface::MaybeGetFromContents(contents_)) {
    OpenChatTab(chat->GetBrowserWindowInterface());
  }
}

void UwChatHandler::DeleteChat(const std::string& id) {
  if (!ChatFile::IsChatId(id)) {
    return;
  }
  if (ChatIndex* index = index_observation_.GetSource()) {
    index->Delete(id);
  }
  std::vector<tabs::TabHandle> showing;
  for (tabs::TabInterface* tab : TabsShowing(
           Profile::FromBrowserContext(contents_->GetBrowserContext()), id)) {
    showing.push_back(tab->GetHandle());
  }
  // This page's own tab may be among them, and closing it ends this handler.
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE,
      base::BindOnce(
          [](std::vector<tabs::TabHandle> tabs) {
            for (tabs::TabHandle handle : tabs) {
              if (tabs::TabInterface* tab = handle.Get()) {
                TabListInterface::From(tab->GetBrowserWindowInterface())
                    ->CloseTab(handle);
              }
            }
          },
          std::move(showing)));
}

void UwChatHandler::OnChatsChanged() {
  ChatIndex* index = index_observation_.GetSource();
  if (!index || !index->loaded()) {
    return;
  }
  std::vector<mojom::ChatSummaryPtr> chats;
  for (const ChatIndex::Entry& entry : index->Entries()) {
    auto chat = mojom::ChatSummary::New();
    chat->id = entry.id;
    chat->title = entry.title;
    chat->updated = entry.updated.InMillisecondsFSinceUnixEpoch();
    chat->state = StateFor(entry.state);
    chats.push_back(std::move(chat));
  }
  page_->OnChats(std::move(chats));
}

void UwChatHandler::OnVisibilityChanged(content::Visibility visibility) {
  if (visibility == content::Visibility::VISIBLE && unread_) {
    unread_ = false;
    UpdateIndex();
  }
}

void UwChatHandler::OnLoaded(std::optional<base::DictValue> saved) {
  loaded_ = true;
  if (saved) {
    if (const std::string* title = saved->FindString("title")) {
      title_ = *title;
    }
    updated_ = base::ValueToTime(saved->Find("updated")).value_or(base::Time());
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
  OnChatsChanged();
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
  if (step.kind != AgentLoop::Step::Kind::kTool) {
    unread_ = contents_->GetVisibility() != content::Visibility::VISIBLE;
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
  ChatIndex* index = index_observation_.GetSource();
  if (!file_ || !context || (index && index->WasDeleted(id_))) {
    return;
  }
  updated_ = base::Time::Now();
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
                  .Set("updated", base::TimeToValue(updated_))
                  .Set("log", log_.Clone())
                  .Set("context", context->ToValue())
                  .Set("tabs", std::move(tabs)));
  UpdateIndex();
}

void UwChatHandler::UpdateIndex() {
  ChatIndex* index = index_observation_.GetSource();
  if (!index || updated_.is_null()) {
    return;
  }
  const base::DictValue* last =
      log_.empty() ? nullptr : log_.back().GetIfDict();
  const std::string* kind = last ? last->FindString("kind") : nullptr;
  ChatIndex::State state = ChatIndex::State::kDone;
  if (Working()) {
    state = ChatIndex::State::kRunning;
  } else if (kind && *kind == "stopped") {
    state = ChatIndex::State::kStopped;
  } else if (unread_) {
    state = ChatIndex::State::kUnread;
  }
  index->Update({id_, title_, updated_, state});
}

}  // namespace uw
