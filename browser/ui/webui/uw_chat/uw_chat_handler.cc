// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/browser/ui/webui/uw_chat/uw_chat_handler.h"

#include <set>
#include <string_view>
#include <utility>
#include <vector>

#include "base/base64.h"
#include "base/base_paths.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/json/json_writer.h"
#include "base/json/values_util.h"
#include "base/no_destructor.h"
#include "base/path_service.h"
#include "base/strings/strcat.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/sequenced_task_runner.h"
#include "base/task/thread_pool.h"
#include "base/time/time.h"
#include "uw/browser/ai_providers/catalog_store.h"
#include "uw/browser/ai_providers/models.h"
// //chrome/browser depends on this target, so it cannot depend back.
#include "chrome/browser/platform_util.h"  // nogncheck
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/tab_list/tab_list_interface.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface_iterator.h"
#include "chrome/browser/ui/navigator/browser_navigator.h"
#include "chrome/browser/ui/navigator/browser_navigator_params.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/common/chrome_paths.h"
#include "components/favicon/content/content_favicon_driver.h"
#include "components/prefs/pref_service.h"
#include "components/prefs/scoped_user_pref_update.h"
#include "components/split_tabs/split_tab_visual_data.h"
#include "components/tabs/public/tab_interface.h"
#include "components/viz/common/frame_sinks/copy_output_result.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/url_constants.h"
#include "ui/base/base_window.h"
#include "ui/base/page_transition_types.h"
#include "ui/base/window_open_disposition.h"
#include "ui/gfx/codec/jpeg_codec.h"
#include "ui/gfx/codec/png_codec.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/size.h"
#include "ui/gfx/image/image.h"
#include "url/gurl.h"
#include "uw/browser/agent/chat_suggestions.h"
#include "uw/browser/agent/chat_tab.h"
#include "uw/browser/ai_providers/api_key_store.h"
#include "uw/browser/tab_organization/ai_connection.h"
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

// Approval modes as the profile and a chat's file keep them.
constexpr std::pair<ApprovalMode, std::string_view> kModeNames[] = {
    {ApprovalMode::kAskFirst, "ask_first"},
    {ApprovalMode::kAuto, "auto"},
    {ApprovalMode::kFullAuto, "full_auto"}};

std::string_view NameOf(ApprovalMode mode) {
  for (const auto& [each, name] : kModeNames) {
    if (each == mode) {
      return name;
    }
  }
  return "auto";
}

// Auto for a name it does not know.
ApprovalMode ModeNamed(std::string_view name) {
  for (const auto& [mode, each] : kModeNames) {
    if (each == name) {
      return mode;
    }
  }
  return ApprovalMode::kAuto;
}

base::DictValue ModelValue(const ModelChoice& choice) {
  base::DictValue value = base::DictValue()
                              .Set("provider", choice.provider_id)
                              .Set("model", choice.model)
                              .Set("effort", choice.effort);
  if (choice.format) {
    value.Set("format", static_cast<int>(*choice.format));
  }
  return value;
}

std::optional<ModelChoice> ModelFrom(const base::DictValue* value) {
  const std::string* provider = value ? value->FindString("provider") : nullptr;
  const std::string* model = value ? value->FindString("model") : nullptr;
  const std::string* effort = value ? value->FindString("effort") : nullptr;
  if (!provider || !model) {
    return std::nullopt;
  }
  return ModelChoice{*provider, *model, FormatFrom(value->FindInt("format")),
                     effort ? *effort : std::string()};
}

mojom::ChatModelPtr PageModel(const std::optional<ModelChoice>& choice) {
  return choice ? mojom::ChatModel::New(choice->provider_id, choice->model,
                                        choice->effort)
                : nullptr;
}

bool IsModelLine(const base::Value& entry) {
  const std::string* kind =
      entry.is_dict() ? entry.GetDict().FindString("kind") : nullptr;
  return kind && *kind == "model";
}

// The images a message may carry: up to 5 data: URLs of the types the page
// sends, none larger than the page makes them. The rest are left out.
std::vector<std::string> UsableImages(const std::vector<std::string>& images) {
  constexpr size_t kMaxImages = 5;
  constexpr size_t kMaxImageLength = 8 * 1024 * 1024;
  std::vector<std::string> usable;
  for (const std::string& image : images) {
    if (usable.size() < kMaxImages && image.size() <= kMaxImageLength &&
        (image.starts_with("data:image/png;base64,") ||
         image.starts_with("data:image/jpeg;base64,"))) {
      usable.push_back(image);
    }
  }
  return usable;
}

// A message from the user as the chat file keeps it.
base::DictValue UserLine(const std::string& text,
                         const std::vector<std::string>& images) {
  base::DictValue line =
      base::DictValue().Set("kind", "user").Set("text", text);
  if (!images.empty()) {
    base::ListValue list;
    for (const std::string& image : images) {
      list.Append(image);
    }
    line.Set("images", std::move(list));
  }
  return line;
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
    case AgentLoop::Step::Kind::kStoppedByUser:
      return mojom::StepKind::kStoppedByUser;
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
    case ChatIndex::State::kWaiting:
      return mojom::ChatState::kWaiting;
    case ChatIndex::State::kStopped:
      return mojom::ChatState::kStopped;
    case ChatIndex::State::kUnread:
      return mojom::ChatState::kUnread;
  }
}

// How wide a picture of a tab is taken: the card's width, twice over for
// sharp screens.
constexpr int kPictureWidth = 480;
constexpr int kPictureQuality = 70;

std::string DataUrl(std::string_view type, base::span<const uint8_t> bytes) {
  return base::StrCat({"data:", type, ";base64,", base::Base64Encode(bytes)});
}

// The agent's tab a tool step acted in: the one it named, or the one it
// opened. None for a step that failed.
int TabOf(const AgentLoop::Step& step) {
  if (step.result.contains("error")) {
    return 0;
  }
  return step.args.FindInt("tab").value_or(
      step.result.FindInt("tab").value_or(0));
}

// uw's own skills, and those made in chats: beside the profiles.
base::FilePath UwSkillsFolder() {
  base::FilePath data;
  base::PathService::Get(chrome::DIR_USER_DATA, &data);
  return data.AppendASCII("skills");
}

base::FilePath& AgentsSkillsFolderForTesting() {
  static base::NoDestructor<base::FilePath> folder;
  return *folder;
}

// The skills other agents share.
base::FilePath AgentsSkillsFolder() {
  if (!AgentsSkillsFolderForTesting().empty()) {
    return AgentsSkillsFolderForTesting();
  }
  base::FilePath home;
  base::PathService::Get(base::DIR_HOME, &home);
  return home.AppendASCII(".agents").AppendASCII("skills");
}

// uw's skills first; a skill in ~/.agents/skills with the same name as one
// of uw's is left out. Blocks.
std::vector<Skill> FindAllSkills(base::FilePath uw, base::FilePath agents) {
  std::vector<Skill> skills = FindSkills(uw, /*agents_folder=*/false);
  for (Skill& skill : FindSkills(agents, /*agents_folder=*/true)) {
    if (std::ranges::none_of(skills, [&](const Skill& kept) {
          return kept.name == skill.name;
        })) {
      skills.push_back(std::move(skill));
    }
  }
  return skills;
}

mojom::SkillSource SourceFor(Skill::Source source) {
  switch (source) {
    case Skill::Source::kUw:
      return mojom::SkillSource::kUw;
    case Skill::Source::kAgents:
      return mojom::SkillSource::kAgents;
    case Skill::Source::kChat:
      return mojom::SkillSource::kChat;
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
  start_ = TakeChatStart(contents_);
  Profile* profile =
      Profile::FromBrowserContext(contents_->GetBrowserContext());
  if (ChatIndex* index = ChatIndex::For(profile)) {
    index_observation_.Observe(index);
  }
  LoadSkills(base::DoNothing());
  if (profile->IsOffTheRecord()) {
    OnLoaded(std::nullopt);
    return;
  }
  file_ = std::make_unique<ChatFile>(profile->GetPath(), id_);
  file_->Load(
      base::BindOnce(&UwChatHandler::OnLoaded, weak_factory_.GetWeakPtr()));
}

// static
void UwChatHandler::SetAgentsSkillsFolderForTesting(base::FilePath folder) {
  AgentsSkillsFolderForTesting() = std::move(folder);
}

UwChatHandler::~UwChatHandler() {
  OpenChats().erase(id_);
}

void UwChatHandler::Send(const std::string& text,
                         const std::vector<std::string>& images) {
  if (!loaded_ || Working()) {
    return;
  }
  if (!skills_loaded_) {
    waiting_message_ = Message{text, images};
    return;
  }
  if (!agent_ && !Start()) {
    unsent_ = Message{text, images};
    return;
  }
  unsent_.reset();
  agent_->SetSkills(EnabledSkills());
  if (title_.empty()) {
    title_ = text;
  }
  const std::vector<std::string> usable = UsableImages(images);
  Log(UserLine(text, usable));
  agent_->Send(handed_over_ + text, usable);
  handed_over_.clear();
  Save();
}

void UwChatHandler::Stop() {
  if (approval_answer_) {
    AnswerApproval(mojom::ApprovalChoice::kStop, std::string());
    return;
  }
  if (Working()) {
    agent_->Stop();
  }
}

// While a step waits for approval, the message declines it as its note.
void UwChatHandler::Steer(const std::string& text,
                          const std::vector<std::string>& images) {
  if (!Working()) {
    Send(text, images);
    return;
  }
  const std::vector<std::string> usable = UsableImages(images);
  Log(UserLine(text, usable));
  Save();
  if (approval_answer_) {
    AnswerApproval(mojom::ApprovalChoice::kDecline, text);
    return;
  }
  agent_->Steer(text, usable);
}

void UwChatHandler::Resume() {
  const base::DictValue* last = LastLine();
  const std::string* kind = last ? last->FindString("kind") : nullptr;
  if (!loaded_ || Working() || !kind || *kind != "stopped") {
    return;
  }
  if (!skills_loaded_) {
    waiting_resume_ = true;
    return;
  }
  if (!agent_ && !Start()) {
    return;
  }
  agent_->SetSkills(EnabledSkills());
  Log(base::DictValue().Set("kind", "resumed"));
  agent_->Resume();
  Save();
}

void UwChatHandler::TryAgain() {
  if (!loaded_ || Working()) {
    return;
  }
  if (std::optional<Message> message = std::exchange(unsent_, {})) {
    Send(message->text, message->images);
    return;
  }
  const base::DictValue* last = LastLine();
  const std::string* kind = last ? last->FindString("kind") : nullptr;
  if (!kind || *kind != "failed" || !skills_loaded_) {
    return;
  }
  if (!agent_ && !Start()) {
    return;
  }
  agent_->SetSkills(EnabledSkills());
  agent_->TryAgain();
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

void UwChatHandler::ShowTab(int32_t number) {
  if (tabs::TabInterface* tab = AgentTab(number)) {
    BrowserWindowInterface* window = tab->GetBrowserWindowInterface();
    TabListInterface::From(window)->ActivateTab(tab->GetHandle());
    window->GetWindow()->Activate();
  }
}

void UwChatHandler::SetSideBySide(bool on, int32_t number) {
  tabs::TabInterface* chat =
      tabs::TabInterface::MaybeGetFromContents(contents_);
  if (!chat) {
    return;
  }
  TabStripModel* model = chat->GetBrowserWindowInterface()->GetTabStripModel();
  const std::optional<split_tabs::SplitTabId> split = chat->GetSplit();
  if (split) {
    model->RemoveSplit(*split);
  }
  tabs::TabInterface* agent = AgentTab(number ? number : current_tab_);
  if (on && agent &&
      agent->GetBrowserWindowInterface() == chat->GetBrowserWindowInterface()) {
    // The split takes the active tab, which is the chat while its page is
    // being used.
    model->ActivateTabAt(model->GetIndexOfTab(chat));
    model->ExecuteAddToNewSplitCommand(model->GetIndexOfTab(agent),
                                       split_tabs::SplitTabLayout::kSideBySide);
  }
  SendTabs();
}

void UwChatHandler::OpenSkillsFolder() {
  base::ThreadPool::PostTaskAndReply(
      FROM_HERE, {base::MayBlock(), base::TaskPriority::USER_BLOCKING},
      base::BindOnce(base::IgnoreResult(&base::CreateDirectory),
                     UwSkillsFolder()),
      base::BindOnce(
          [](base::WeakPtr<UwChatHandler> self) {
            if (self) {
              platform_util::OpenItem(Profile::FromBrowserContext(
                                          self->contents_->GetBrowserContext()),
                                      UwSkillsFolder(),
                                      platform_util::OPEN_FOLDER, {});
            }
          },
          weak_factory_.GetWeakPtr()));
}

void UwChatHandler::GetSuggestions(GetSuggestionsCallback callback) {
  ChatSuggestions* suggestions = ChatSuggestions::For(
      Profile::FromBrowserContext(contents_->GetBrowserContext()));
  if (!suggestions) {
    std::move(callback).Run({});
    return;
  }
  suggestions->Get(std::move(callback));
}

void UwChatHandler::GetSkills(GetSkillsCallback callback) {
  LoadSkills(base::BindOnce(&UwChatHandler::ReplySkills,
                            weak_factory_.GetWeakPtr(), std::move(callback)));
}

void UwChatHandler::SetSkillOn(const std::string& name, bool on) {
  ScopedListPrefUpdate off(
      Profile::FromBrowserContext(contents_->GetBrowserContext())->GetPrefs(),
      kAgentDisabledSkillsPref);
  off->EraseValue(base::Value(name));
  if (!on) {
    off->Append(name);
  }
}

void UwChatHandler::AnswerSkill(bool save) {
  if (!skill_answer_) {
    return;
  }
  if (!save) {
    std::move(skill_answer_)
        .Run(base::DictValue()
                 .Set("saved", false)
                 .Set("reason", "The user chose not to save it."));
    return;
  }
  const std::string* name = proposed_skill_.FindString("name");
  const std::string* description = proposed_skill_.FindString("description");
  const std::string* instructions = proposed_skill_.FindString("instructions");
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::MayBlock()},
      base::BindOnce(
          [](base::FilePath folder, std::string name, std::string description,
             std::string instructions) {
            return WriteSkill(folder, name, description, instructions);
          },
          UwSkillsFolder(), *name, *description, *instructions),
      base::BindOnce(&UwChatHandler::OnSkillWritten,
                     weak_factory_.GetWeakPtr()));
}

void UwChatHandler::AnswerApproval(mojom::ApprovalChoice choice,
                                   const std::string& note) {
  if (!approval_answer_) {
    return;
  }
  AgentLoop::ApprovalAnswer answer;
  answer.note = note;
  switch (choice) {
    case mojom::ApprovalChoice::kApprove:
      answer.choice = AgentLoop::ApprovalAnswer::Choice::kApprove;
      break;
    case mojom::ApprovalChoice::kDecline:
      answer.choice = AgentLoop::ApprovalAnswer::Choice::kDecline;
      break;
    case mojom::ApprovalChoice::kStop:
      answer.choice = AgentLoop::ApprovalAnswer::Choice::kStop;
      break;
  }
  std::move(approval_answer_).Run(std::move(answer));
  UpdateIndex();
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
  mode_ = ModeNamed(Profile::FromBrowserContext(contents_->GetBrowserContext())
                        ->GetPrefs()
                        ->GetString(kAgentApprovalModePref));
  if (saved) {
    if (const std::string* title = saved->FindString("title")) {
      title_ = *title;
    }
    updated_ = base::ValueToTime(saved->Find("updated")).value_or(base::Time());
    if (const base::ListValue* log = saved->FindList("log")) {
      log_ = log->Clone();
    }
    model_ = ModelFrom(saved->FindDict("model"));
    if (const std::string* mode = saved->FindString("mode")) {
      mode_ = ModeNamed(*mode);
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
  if (!model_) {
    model_ =
        AgentChoice(*Profile::FromBrowserContext(contents_->GetBrowserContext())
                         ->GetPrefs());
  }
  page_->OnOpened(id_, title_, std::move(lines));
  page_->OnModel(PageModel(model_));
  page_->OnMode(static_cast<mojom::ApprovalMode>(mode_));
  OnChatsChanged();
  if (start_) {
    page_->OnAsk(start_->text);
  }
}

bool UwChatHandler::Start() {
  tabs::TabInterface* tab = tabs::TabInterface::MaybeGetFromContents(contents_);
  if (!tab) {
    page_->OnStep(Failed("A chat works only in a tab."));
    return false;
  }
  // A chat opened before any model was picked takes the agent model now.
  if (!model_) {
    model_ =
        AgentChoice(*Profile::FromBrowserContext(contents_->GetBrowserContext())
                         ->GetPrefs());
    page_->OnModel(PageModel(model_));
  }
  std::optional<AgentModel> model = AgentModelNow();
  if (!model) {
    page_->OnStep(Failed("Pick a model on AI providers first."));
    return false;
  }
  agent_ = std::make_unique<AgentLoop>(
      *tab, std::move(*model),
      base::BindRepeating(&UwChatHandler::OnStep, base::Unretained(this)),
      std::move(saved_context_),
      base::BindRepeating(&UwChatHandler::Save, base::Unretained(this)));
  agent_->SetSkillSaver(base::BindRepeating(&UwChatHandler::AskToSaveSkill,
                                            base::Unretained(this)));
  agent_->SetApprover(
      base::BindRepeating(&UwChatHandler::AskApproval, base::Unretained(this)));
  agent_->SetMode(mode_);
  if (TabOrganizationController* tree =
          TabOrganizationController::From(tab->GetBrowserWindowInterface())) {
    for (const auto& [number, page] : saved_tabs_) {
      if (tabs::TabInterface* restored = tree->TabFor(PageId(page))) {
        agent_->tools().Adopt(number, *restored);
      }
    }
  }
  saved_tabs_.clear();
  // The tab the user asked from, from the command bar, is the agent's tab 1.
  if (start_) {
    if (tabs::TabInterface* asked_from = start_->tab.Get()) {
      agent_->tools().Adopt(1, *asked_from);
      content::WebContents* page = asked_from->GetContents();
      handed_over_ = base::StrCat({"I asked this from tab 1, \"",
                                   base::UTF16ToUTF8(page->GetTitle()),
                                   "\" at ", page->GetLastCommittedURL().spec(),
                                   ". Read tab 1 first.\n\n"});
    }
    start_.reset();
  }
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
    case AgentLoop::Step::Kind::kStoppedByUser:
      Log(base::DictValue().Set("kind", "stopped_by_user"));
      break;
  }
  if (step.kind != AgentLoop::Step::Kind::kTool) {
    unread_ = contents_->GetVisibility() != content::Visibility::VISIBLE;
  } else {
    OnToolStep(step);
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

std::optional<AgentModel> UwChatHandler::AgentModelNow() const {
  const PrefService& prefs =
      *Profile::FromBrowserContext(contents_->GetBrowserContext())->GetPrefs();
  AgentModel model;
  if (model_) {
    model.connection = ConfigForChoice(prefs, *model_);
  }
  if (!IsComplete(model.connection)) {
    return std::nullopt;
  }
  if (!model.connection.key_account.empty()) {
    model.api_key = ApiKeyStore()
                        .GetKey(model.connection.key_account)
                        .value_or(std::string());
  }
  return model;
}

void UwChatHandler::SetModel(const std::string& provider_id,
                             const std::string& model) {
  Profile* profile =
      Profile::FromBrowserContext(contents_->GetBrowserContext());
  const std::optional<Provider> provider =
      FindProvider(*profile->GetPrefs(), provider_id);
  if (!loaded_ || !provider || model.empty()) {
    return;
  }
  CatalogStore::For(profile)->FormatOf(
      *provider, model,
      base::BindOnce(&UwChatHandler::OnModelFormat, weak_factory_.GetWeakPtr(),
                     provider_id, model));
}

void UwChatHandler::OnModelFormat(const std::string& provider_id,
                                  const std::string& model,
                                  std::optional<ProviderFormat> format) {
  model_ = ModelChoice{provider_id, model, format, std::string()};
  OnModelChanged();
}

void UwChatHandler::SetEffort(const std::string& effort) {
  if (!loaded_ || !model_) {
    return;
  }
  model_->effort = effort;
  OnModelChanged();
}

// On a new chat, before its first message, the pick is also the mode new
// chats start on.
void UwChatHandler::SetMode(mojom::ApprovalMode mode) {
  if (!loaded_) {
    return;
  }
  mode_ = static_cast<ApprovalMode>(mode);
  if (agent_) {
    agent_->SetMode(mode_);
  }
  if (title_.empty()) {
    Profile::FromBrowserContext(contents_->GetBrowserContext())
        ->GetPrefs()
        ->SetString(kAgentApprovalModePref, NameOf(mode_));
  }
  Save();
  page_->OnMode(mode);
}

// Switches in a row make one line, as the page shows them.
void UwChatHandler::OnModelChanged() {
  if (agent_) {
    if (std::optional<AgentModel> model = AgentModelNow()) {
      agent_->SetModel(std::move(*model));
    }
  }
  if (!title_.empty()) {
    base::DictValue line = base::DictValue()
                               .Set("kind", "model")
                               .Set("model", ModelValue(*model_));
    if (!log_.empty() && IsModelLine(log_.back())) {
      log_.back() = base::Value(std::move(line));
    } else {
      Log(std::move(line));
    }
  }
  Save();
  page_->OnModel(PageModel(model_));
}

const base::DictValue* UwChatHandler::LastLine() const {
  for (size_t i = log_.size(); i > 0; --i) {
    if (!IsModelLine(log_[i - 1])) {
      return log_[i - 1].GetIfDict();
    }
  }
  return nullptr;
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
    if (const base::ListValue* images = entry.FindList("images")) {
      for (const base::Value& image : *images) {
        if (image.is_string()) {
          line->images.push_back(image.GetString());
        }
      }
    }
  } else if (*kind == "answer") {
    line->kind = mojom::StepKind::kAnswer;
  } else if (*kind == "stopped_by_user") {
    line->kind = mojom::StepKind::kStoppedByUser;
    line->text = "You stopped the chat.";
  } else if (*kind == "stopped") {
    line->kind = mojom::StepKind::kStopped;
  } else if (*kind == "resumed") {
    line->kind = mojom::StepKind::kResumed;
  } else if (*kind == "model") {
    line->kind = mojom::StepKind::kModel;
    line->model = PageModel(ModelFrom(entry.FindDict("model")));
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
  base::DictValue saved = base::DictValue()
                              .Set("version", 1)
                              .Set("title", title_)
                              .Set("running", Working())
                              .Set("updated", base::TimeToValue(updated_))
                              .Set("log", log_.Clone())
                              .Set("context", context->ToValue())
                              .Set("tabs", std::move(tabs));
  if (model_) {
    saved.Set("model", ModelValue(*model_));
  }
  saved.Set("mode", NameOf(mode_));
  file_->Save(saved);
  UpdateIndex();
}

tabs::TabInterface* UwChatHandler::AgentTab(int number) {
  if (!agent_) {
    return nullptr;
  }
  const std::map<int, tabs::TabInterface*> open = agent_->tools().OpenTabs();
  const auto tab = open.find(number);
  return tab == open.end() ? nullptr : tab->second;
}

void UwChatHandler::OnToolStep(const AgentLoop::Step& step) {
  const int number = TabOf(step);
  tabs::TabInterface* tab = AgentTab(number);
  if (!tab) {
    return;
  }
  const bool moved = number != current_tab_;
  current_tab_ = number;
  content::WebContents* contents = tab->GetContents();
  Card& card = cards_[number];
  card.title = base::UTF16ToUTF8(contents->GetTitle());
  card.url = contents->GetVisibleURL().spec();
  const favicon::FaviconDriver* icons =
      favicon::ContentFaviconDriver::FromWebContents(contents);
  const gfx::Image favicon = icons ? icons->GetFavicon() : gfx::Image();
  if (!favicon.IsEmpty()) {
    if (std::optional<std::vector<uint8_t>> png =
            gfx::PNGCodec::EncodeBGRASkBitmap(*favicon.ToSkBitmap(),
                                              /*discard_transparency=*/false)) {
      card.favicon = DataUrl("image/png", *png);
    }
  }
  if (content::RenderWidgetHostView* view =
          contents->GetRenderWidgetHostView()) {
    const gfx::Size size = view->GetViewBounds().size();
    if (!size.IsEmpty()) {
      view->CopyFromSurface(
          gfx::Rect(),
          gfx::Size(kPictureWidth,
                    kPictureWidth * size.height() / size.width()),
          base::Seconds(5),
          base::BindOnce(&UwChatHandler::OnPicture, weak_factory_.GetWeakPtr(),
                         number));
    }
  }
  // Side by side, the chat follows the agent to its other tabs.
  tabs::TabInterface* chat =
      tabs::TabInterface::MaybeGetFromContents(contents_);
  if (moved && chat && chat->GetSplit() &&
      tab->GetSplit() != chat->GetSplit()) {
    SetSideBySide(true, number);
    return;
  }
  SendTabs();
}

void UwChatHandler::OnPicture(int number,
                              const content::CopyFromSurfaceResult& result) {
  if (!result.has_value() || result->bitmap.drawsNothing()) {
    return;
  }
  if (std::optional<std::vector<uint8_t>> jpeg =
          gfx::JPEGCodec::Encode(result->bitmap, kPictureQuality)) {
    cards_[number].picture = DataUrl("image/jpeg", *jpeg);
    SendTabs();
  }
}

void UwChatHandler::SendTabs() {
  const std::map<int, tabs::TabInterface*> open =
      agent_ ? agent_->tools().OpenTabs()
             : std::map<int, tabs::TabInterface*>();
  std::vector<mojom::AgentTabPtr> cards;
  for (const auto& [number, card] : cards_) {
    auto sent = mojom::AgentTab::New();
    sent->number = number;
    sent->title = card.title;
    sent->url = card.url;
    sent->favicon = card.favicon;
    sent->picture = card.picture;
    sent->closed = !open.contains(number);
    cards.push_back(std::move(sent));
  }
  tabs::TabInterface* chat =
      tabs::TabInterface::MaybeGetFromContents(contents_);
  page_->OnTabs(std::move(cards), current_tab_,
                chat && chat->GetSplit().has_value());
}

void UwChatHandler::LoadSkills(base::OnceClosure done) {
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::MayBlock()},
      base::BindOnce(&FindAllSkills, UwSkillsFolder(), AgentsSkillsFolder()),
      base::BindOnce(&UwChatHandler::OnSkillsLoaded, weak_factory_.GetWeakPtr(),
                     std::move(done)));
}

void UwChatHandler::OnSkillsLoaded(base::OnceClosure done,
                                   std::vector<Skill> skills) {
  skills_ = std::move(skills);
  skills_loaded_ = true;
  if (agent_) {
    agent_->SetSkills(EnabledSkills());
  }
  std::move(done).Run();
  if (std::optional<Message> message = std::exchange(waiting_message_, {})) {
    Send(message->text, message->images);
  }
  if (std::exchange(waiting_resume_, false)) {
    Resume();
  }
}

std::vector<Skill> UwChatHandler::EnabledSkills() const {
  const base::ListValue& off =
      Profile::FromBrowserContext(contents_->GetBrowserContext())
          ->GetPrefs()
          ->GetList(kAgentDisabledSkillsPref);
  std::vector<Skill> on;
  for (const Skill& skill : skills_) {
    if (!off.contains(skill.name)) {
      on.push_back(skill);
    }
  }
  return on;
}

void UwChatHandler::ReplySkills(GetSkillsCallback callback) {
  const base::ListValue& off =
      Profile::FromBrowserContext(contents_->GetBrowserContext())
          ->GetPrefs()
          ->GetList(kAgentDisabledSkillsPref);
  std::vector<mojom::SkillInfoPtr> skills;
  for (const Skill& skill : skills_) {
    skills.push_back(mojom::SkillInfo::New(skill.name, skill.description,
                                           SourceFor(skill.source),
                                           !off.contains(skill.name)));
  }
  std::move(callback).Run(std::move(skills), UwSkillsFolder().AsUTF8Unsafe());
}

void UwChatHandler::AskToSaveSkill(
    base::DictValue args,
    base::OnceCallback<void(base::DictValue)> answer) {
  const std::string* name = args.FindString("name");
  const std::string* description = args.FindString("description");
  const std::string* instructions = args.FindString("instructions");
  std::string_view error;
  if (!name || !IsSkillName(*name)) {
    error = "name must be lowercase letters, digits, and hyphens.";
  } else if (!description || description->empty() || !instructions ||
             instructions->empty()) {
    error = "save_skill needs a description and instructions.";
  } else if (std::ranges::any_of(skills_, [&](const Skill& skill) {
               return skill.name == *name;
             })) {
    error = "A skill with that name exists already.";
  } else if (skill_answer_) {
    error = "The user has not answered the last save_skill yet.";
  }
  if (!error.empty()) {
    std::move(answer).Run(base::DictValue().Set("error", error));
    return;
  }
  page_->OnSkillProposal(
      mojom::SkillProposal::New(*name, *description, *instructions));
  proposed_skill_ = std::move(args);
  skill_answer_ = std::move(answer);
}

void UwChatHandler::AskApproval(
    AgentTools::Approval approval,
    base::OnceCallback<void(AgentLoop::ApprovalAnswer)> answer) {
  approval_answer_ = std::move(answer);
  page_->OnApproval(mojom::Approval::New(approval.tab, approval.site,
                                         approval.action, approval.reason));
  UpdateIndex();
}

void UwChatHandler::OnSkillWritten(bool written) {
  if (!skill_answer_) {
    return;
  }
  if (!written) {
    std::move(skill_answer_)
        .Run(base::DictValue().Set("error", "The skill could not be saved."));
    return;
  }
  LoadSkills(base::BindOnce(
      [](base::OnceCallback<void(base::DictValue)> answer) {
        std::move(answer).Run(base::DictValue().Set("saved", true));
      },
      std::move(skill_answer_)));
}

void UwChatHandler::UpdateIndex() {
  ChatIndex* index = index_observation_.GetSource();
  if (!index || updated_.is_null()) {
    return;
  }
  const base::DictValue* last = LastLine();
  const std::string* kind = last ? last->FindString("kind") : nullptr;
  ChatIndex::State state = ChatIndex::State::kDone;
  if (approval_answer_) {
    state = ChatIndex::State::kWaiting;
  } else if (Working()) {
    state = ChatIndex::State::kRunning;
  } else if (kind && *kind == "stopped") {
    state = ChatIndex::State::kStopped;
  } else if (unread_) {
    state = ChatIndex::State::kUnread;
  }
  index->Update({id_, title_, updated_, state});
}

}  // namespace uw
