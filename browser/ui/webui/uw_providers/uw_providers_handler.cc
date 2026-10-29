// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/browser/ui/webui/uw_providers/uw_providers_handler.h"

#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/task/sequenced_task_runner.h"
#include "base/task/thread_pool.h"
#include "base/values.h"
#include "chrome/browser/platform_util.h"  // nogncheck
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/navigator/browser_navigator.h"
#include "chrome/browser/ui/navigator/browser_navigator_params.h"
#include "chrome/browser/ui/singleton_tabs.h"  // nogncheck
#include "components/prefs/pref_service.h"
#include "components/prefs/scoped_user_pref_update.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/storage_partition.h"
#include "content/public/common/url_constants.h"
#include "mojo/public/cpp/bindings/callback_helpers.h"
#include "net/base/filename_util.h"
#include "net/base/net_errors.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"
#include "ui/base/page_transition_types.h"
#include "ui/base/window_open_disposition.h"
#include "url/gurl.h"
#include "uw/browser/agent/chat_file.h"
#include "uw/browser/agent/chat_index.h"
#include "uw/browser/agent/chat_tab.h"
#include "uw/browser/agent/skill_folders.h"
#include "uw/browser/ai_providers/api_key_store.h"
#include "uw/browser/ai_providers/catalog_store.h"
#include "uw/browser/ai_providers/decision_probes.h"
#include "uw/browser/ai_providers/models.h"
#include "uw/browser/tab_organization/ai_connection.h"
#include "uw/browser/terminal/terminal_tab.h"
#include "uw/browser/ui/webui/uw_providers/uw_providers_ui.h"
#include "uw/components/agent/agent_protocol.h"
#include "uw/components/ai_providers/formats.h"
#include "uw/components/connectors/catalog.h"
#include "uw/components/connectors/connector_config.h"
#include "uw/components/tab_organization/ai_protocol.h"

namespace uw {

struct UwProvidersHandler::ModelTest {
  GURL url;
  std::string key_account;
  // In chat completions' shape for a language model, which gets it in its
  // own format.
  std::optional<std::string> body;
  std::optional<AiConnectionConfig> language;
  base::RepeatingCallback<bool(const std::string&)> reads;
};

namespace {

constexpr size_t kMaxDetailBytes = 6000;
constexpr char kHiddenKey[] = "<api key>";

ModelSlot SlotFrom(mojom::ModelSlot slot) {
  switch (slot) {
    case mojom::ModelSlot::kLanguage:
      return ModelSlot::kLanguage;
    case mojom::ModelSlot::kDecision:
      return ModelSlot::kDecision;
    case mojom::ModelSlot::kAgent:
      return ModelSlot::kAgent;
  }
}

std::string StoredKey(const Provider& provider) {
  return provider.key_account.empty()
             ? std::string()
             : ApiKeyStore().GetKey(provider.key_account).value_or("");
}

// A preset keeps its own name, address, and format.
Provider ProviderFrom(const mojom::NewProvider& input) {
  const auto preset = static_cast<ProviderPreset>(input.preset);
  Provider provider = PresetProvider(preset);
  if (preset == ProviderPreset::kCustom) {
    provider.name = input.name;
    provider.format = static_cast<ProviderFormat>(input.format);
  }
  if (preset == ProviderPreset::kCustom || !input.address.empty()) {
    provider.address = input.address;
  }
  return provider;
}

mojom::ProviderPtr PageProvider(const Provider& provider) {
  auto page = mojom::Provider::New();
  page->id = provider.id;
  page->preset = static_cast<mojom::ProviderPreset>(provider.preset);
  page->name = provider.name;
  page->address = provider.address;
  page->format = static_cast<mojom::ProviderFormat>(provider.format);
  page->has_key = !StoredKey(provider).empty();
  page->serves_language = LanguageFormatOf(provider).has_value();
  page->serves_decisions = DecisionFormatOf(provider).has_value();
  return page;
}

mojom::ModelChoicePtr PageChoice(const PrefService& prefs, ModelSlot slot) {
  const std::optional<ModelChoice> choice = ReadChoice(prefs, slot);
  return choice ? mojom::ModelChoice::New(
                      choice->provider_id, choice->model, choice->effort,
                      slot == ModelSlot::kDecision &&
                          IsLanguageQuickChoice(prefs, *choice))
                : nullptr;
}

// A language model can make the quick choices too.
bool ServesSlot(const Provider& provider, ModelSlot slot, bool language) {
  return slot == ModelSlot::kDecision && !language
             ? DecisionFormatOf(provider).has_value()
             : LanguageFormatOf(provider).has_value();
}

BranchContext CheckContext() {
  BranchContext context;
  context.title = "Connectivity check";
  context.url = "about:blank";
  context.source_description = "setup validation";
  return context;
}

// Indents JSON for reading and keeps the start of anything long.
std::string Readable(const std::string& text) {
  const std::optional<base::Value> parsed =
      base::JSONReader::Read(text, base::JSON_PARSE_RFC);
  const std::optional<std::string> indented =
      parsed ? base::WriteJsonWithOptions(*parsed, base::OPTIONS_PRETTY_PRINT)
             : std::nullopt;
  return std::string(
      base::TruncateUTF8ToByteSize(indented.value_or(text), kMaxDetailBytes));
}

mojom::ModelListPtr PageList(ModelListing listing) {
  auto list = mojom::ModelList::New();
  list->ok = listing.ok;
  list->error = std::move(listing.error);
  for (ListedModel& model : listing.models) {
    list->models.push_back(mojom::Model::New(
        std::move(model.name), model.abilities.calls_tools,
        model.abilities.sees_images, model.abilities.reads_pdfs,
        std::move(model.abilities.efforts),
        static_cast<mojom::ProviderFormat>(model.format), model.writes_text,
        static_cast<mojom::Decides>(model.decides)));
  }
  list->checking = listing.checking;
  return list;
}

// Sent to the slot's model as automatic organization reaches it. A language
// model passes when it answers with text, a decision model when it answers
// with a choice, and a language model making the quick choices when it
// answers with a placement.
UwProvidersHandler::ModelTest TestFor(ModelSlot slot,
                                      const PrefService& prefs) {
  UwProvidersHandler::ModelTest test;
  const std::optional<ModelChoice> quick =
      ReadChoice(prefs, ModelSlot::kDecision);
  if (slot == ModelSlot::kDecision && quick &&
      IsLanguageQuickChoice(prefs, *quick)) {
    const AiConnectionConfig config = ReadQuickLanguageConfig(&prefs);
    if (!IsComplete(config)) {
      return test;
    }
    test.url = LanguageModelUrl(config.endpoint, config.format);
    test.key_account = config.key_account;
    test.language = config;
    test.body = BuildDecisionRequestBody(DecisionFormat::kChat, config.model,
                                         CheckContext(), {});
    test.reads = base::BindRepeating([](const std::string& body) {
      return ParseDecisionResponse(DecisionFormat::kChat, body,
                                   /*has_tasks=*/false)
          .has_value();
    });
    return test;
  }
  if (slot == ModelSlot::kDecision) {
    const DecisionModelConfig config = ReadDecisionModelConfig(&prefs);
    if (!IsComplete(config)) {
      return test;
    }
    test.url = DecisionUrl(config);
    test.key_account = config.key_account;
    test.body = BuildDecisionRequestBody(config.format, config.model,
                                         CheckContext(), {});
    test.reads = base::BindRepeating(
        [](DecisionFormat format, const std::string& body) {
          return ParseDecisionResponse(format, body, /*has_tasks=*/false)
              .has_value();
        },
        config.format);
    return test;
  }
  const AiConnectionConfig config = ReadAiConnectionConfig(&prefs);
  if (!IsComplete(config)) {
    return test;
  }
  base::ListValue messages;
  messages.Append(UserMessage("Say hello in one word."));
  test.url = LanguageModelUrl(config.endpoint, config.format);
  test.key_account = config.key_account;
  test.language = config;
  test.body = BuildAgentRequestBody(config.model, messages, base::ListValue());
  test.reads = base::BindRepeating([](const std::string& body) {
    const std::optional<ModelTurn> turn = ParseAgentResponse(body);
    return turn && !turn->text.empty();
  });
  return test;
}

}  // namespace

UwProvidersHandler::UwProvidersHandler(
    Profile* profile,
    content::WebContents* contents,
    mojo::PendingReceiver<mojom::ProvidersHandler> receiver)
    : profile_(profile),
      contents_(contents),
      receiver_(this, std::move(receiver)),
      lister_(profile) {
  connectors_observation_.Observe(&Connectors::Get());
  catalog_observation_.Observe(&ConnectorCatalogStore::Get());
  if (MemoryStore* memory = MemoryStore::For(profile_)) {
    memory_observation_.Observe(memory);
  }
  prefs_.Init(profile_->GetPrefs());
  prefs_.Add(kAgentMemoryPref,
             base::BindRepeating(&UwProvidersHandler::OnMemoriesChanged,
                                 base::Unretained(this)));
}

UwProvidersHandler::~UwProvidersHandler() = default;

void UwProvidersHandler::GetState(GetStateCallback callback) {
  const PrefService& prefs = *profile_->GetPrefs();
  auto state = mojom::ProvidersState::New();
  for (const Provider& provider : ReadProviders(prefs)) {
    state->providers.push_back(PageProvider(provider));
  }
  for (ProviderPreset preset :
       {ProviderPreset::kOpenAi, ProviderPreset::kAnthropic,
        ProviderPreset::kOpenRouter, ProviderPreset::kVercelAiGateway,
        ProviderPreset::kOllama, ProviderPreset::kJev}) {
    state->preset_addresses.push_back(
        mojom::PresetAddress::New(static_cast<mojom::ProviderPreset>(preset),
                                  PresetProvider(preset).address));
  }
  state->language = PageChoice(prefs, ModelSlot::kLanguage);
  state->decision = PageChoice(prefs, ModelSlot::kDecision);
  state->agent = PageChoice(prefs, ModelSlot::kAgent);
  state->upgrade_notice =
      static_cast<mojom::UpgradeNotice>(prefs.GetInteger(kUpgradeNoticePref));
  std::move(callback).Run(std::move(state));
}

void UwProvidersHandler::ListModels(const std::string& provider_id,
                                    ListModelsCallback callback) {
  callback = mojo::WrapCallbackWithDefaultInvokeIfNotRun(
      std::move(callback), mojom::ModelList::New());
  const std::optional<Provider> provider =
      FindProvider(*profile_->GetPrefs(), provider_id);
  if (!provider || profile_->IsOffTheRecord()) {
    return;
  }
  lister_.List(*provider, StoredKey(*provider),
               base::BindOnce(&PageList).Then(std::move(callback)));
}

void UwProvidersHandler::ListModelsAt(mojom::NewProviderPtr input,
                                      ListModelsAtCallback callback) {
  callback = mojo::WrapCallbackWithDefaultInvokeIfNotRun(
      std::move(callback), mojom::ModelList::New());
  if (profile_->IsOffTheRecord()) {
    return;
  }
  lister_.List(ProviderFrom(*input), input->api_key,
               base::BindOnce(&PageList).Then(std::move(callback)));
}

void UwProvidersHandler::WaitForDecisionChecks(
    const std::string& provider_id,
    WaitForDecisionChecksCallback callback) {
  DecisionProbes::For(profile_)->WaitForAnswer(
      provider_id,
      mojo::WrapCallbackWithDefaultInvokeIfNotRun(std::move(callback)));
}

void UwProvidersHandler::AddProvider(mojom::NewProviderPtr input,
                                     AddProviderCallback callback) {
  Provider provider = ProviderFrom(*input);
  if (profile_->IsOffTheRecord() ||
      !IsValidProviderEndpoint(provider.address) || provider.name.empty()) {
    std::move(callback).Run(std::nullopt);
    return;
  }
  PrefService& prefs = *profile_->GetPrefs();
  provider.id = uw::AddProvider(prefs, provider);
  if (!input->api_key.empty() &&
      ApiKeyStore().SetKey(KeyAccountFor(provider.id), input->api_key)) {
    provider.key_account = KeyAccountFor(provider.id);
    UpdateProvider(prefs, provider);
  }
  std::move(callback).Run(provider.id);
}

void UwProvidersHandler::ReplaceKey(const std::string& provider_id,
                                    const std::string& api_key,
                                    ReplaceKeyCallback callback) {
  PrefService& prefs = *profile_->GetPrefs();
  std::optional<Provider> provider = FindProvider(prefs, provider_id);
  if (provider && !api_key.empty() &&
      ApiKeyStore().SetKey(KeyAccountFor(provider_id), api_key)) {
    provider->key_account = KeyAccountFor(provider_id);
    UpdateProvider(prefs, *provider);
    DecisionProbes::For(profile_)->Forget(provider_id);
  }
  std::move(callback).Run();
}

void UwProvidersHandler::ChangeAddress(const std::string& provider_id,
                                       const std::string& address,
                                       ChangeAddressCallback callback) {
  callback = mojo::WrapCallbackWithDefaultInvokeIfNotRun(
      std::move(callback), mojom::ModelList::New());
  std::optional<Provider> provider =
      FindProvider(*profile_->GetPrefs(), provider_id);
  if (!provider || profile_->IsOffTheRecord()) {
    return;
  }
  const std::string key = StoredKey(*provider);
  provider->address = address;
  lister_.List(*provider, key,
               base::BindOnce(&UwProvidersHandler::OnAddressListed,
                              weak_factory_.GetWeakPtr(), *provider,
                              std::move(callback)));
}

void UwProvidersHandler::OnAddressListed(Provider moved,
                                         ChangeAddressCallback callback,
                                         ModelListing listing) {
  if (listing.ok) {
    UpdateProvider(*profile_->GetPrefs(), moved);
  }
  std::move(callback).Run(PageList(std::move(listing)));
}

void UwProvidersHandler::RemoveProvider(const std::string& provider_id,
                                        RemoveProviderCallback callback) {
  ApiKeyStore().DeleteKey(KeyAccountFor(provider_id));
  uw::RemoveProvider(*profile_->GetPrefs(), provider_id);
  DecisionProbes::For(profile_)->Forget(provider_id);
  std::move(callback).Run();
}

void UwProvidersHandler::PickModel(mojom::ModelSlot page_slot,
                                   const std::string& provider_id,
                                   const std::string& model,
                                   bool language,
                                   PickModelCallback callback) {
  PrefService& prefs = *profile_->GetPrefs();
  const ModelSlot slot = SlotFrom(page_slot);
  language = language && slot == ModelSlot::kDecision;
  const std::optional<Provider> provider = FindProvider(prefs, provider_id);
  if (!provider || !ServesSlot(*provider, slot, language) || model.empty()) {
    std::move(callback).Run();
    return;
  }
  CatalogStore::For(profile_)->FormatOf(
      *provider, model,
      base::BindOnce(&UwProvidersHandler::OnPickFormat,
                     weak_factory_.GetWeakPtr(), slot, provider_id, model,
                     language)
          .Then(std::move(callback)));
}

void UwProvidersHandler::OnPickFormat(ModelSlot slot,
                                      const std::string& provider_id,
                                      const std::string& model,
                                      bool language,
                                      std::optional<ProviderFormat> format) {
  SetChoice(*profile_->GetPrefs(), slot, provider_id, model, format, language);
}

void UwProvidersHandler::ClearModel(mojom::ModelSlot slot,
                                    ClearModelCallback callback) {
  ClearChoice(*profile_->GetPrefs(), SlotFrom(slot));
  std::move(callback).Run();
}

void UwProvidersHandler::SetEffort(mojom::ModelSlot slot,
                                   const std::string& effort,
                                   SetEffortCallback callback) {
  uw::SetEffort(*profile_->GetPrefs(), SlotFrom(slot), effort);
  std::move(callback).Run();
}

void UwProvidersHandler::TestModel(mojom::ModelSlot page_slot,
                                   TestModelCallback callback) {
  const PrefService& prefs = *profile_->GetPrefs();
  const ModelSlot slot = SlotFrom(page_slot);
  const std::optional<ModelChoice> choice = ReadChoice(prefs, slot);
  ModelTest test = TestFor(slot, prefs);
  if (!choice || !test.body || slot == ModelSlot::kAgent ||
      profile_->IsOffTheRecord()) {
    auto incomplete = mojom::ModelTestResult::New();
    incomplete->outcome = mojom::TestOutcome::kIncomplete;
    incomplete->model = choice ? choice->model : std::string();
    std::move(callback).Run(std::move(incomplete));
    return;
  }
  std::string key = test.key_account.empty()
                        ? std::string()
                        : ApiKeyStore().GetKey(test.key_account).value_or("");
  std::unique_ptr<RemoteModelClient> client =
      test.language
          ? RemoteModelClient::ForLanguageModel(LoaderFactory(), *test.language,
                                                key, *test.body,
                                                kProviderTimeout)
          : std::make_unique<RemoteModelClient>(LoaderFactory(), test.url, key,
                                                *test.body, kProviderTimeout);
  Send(std::move(client), /*post=*/true,
       base::BindOnce(&UwProvidersHandler::OnTested, weak_factory_.GetWeakPtr(),
                      slot, *choice, std::move(test), key,
                      base::TimeTicks::Now(), std::move(callback)));
}

void UwProvidersHandler::DismissUpgradeNotice(
    DismissUpgradeNoticeCallback callback) {
  profile_->GetPrefs()->ClearPref(kUpgradeNoticePref);
  std::move(callback).Run();
}

scoped_refptr<network::SharedURLLoaderFactory>
UwProvidersHandler::LoaderFactory() {
  return profile_->GetDefaultStoragePartition()
      ->GetURLLoaderFactoryForBrowserProcess();
}

void UwProvidersHandler::Send(std::unique_ptr<RemoteModelClient> client,
                              bool post,
                              RemoteModelClient::ResultCallback done) {
  const int request = next_request_++;
  RemoteModelClient* sender = client.get();
  requests_[request] = std::move(client);
  auto answered =
      base::BindOnce(&UwProvidersHandler::OnAnswered,
                     weak_factory_.GetWeakPtr(), request, std::move(done));
  if (post) {
    sender->Send(std::move(answered));
  } else {
    sender->SendGet(std::move(answered));
  }
}

void UwProvidersHandler::OnAnswered(int request,
                                    RemoteModelClient::ResultCallback done,
                                    RemoteModelClient::Outcome outcome) {
  requests_.erase(request);
  std::move(done).Run(std::move(outcome));
}

void UwProvidersHandler::OnTested(ModelSlot slot,
                                  ModelChoice choice,
                                  ModelTest test,
                                  std::string key,
                                  base::TimeTicks sent_at,
                                  TestModelCallback callback,
                                  RemoteModelClient::Outcome outcome) {
  if (!key.empty()) {
    base::ReplaceSubstringsAfterOffset(&outcome.body, 0, key, kHiddenKey);
  }
  auto result = mojom::ModelTestResult::New();
  result->model = choice.model;
  result->host = test.url.host();
  result->http_status = outcome.http_status;
  result->seconds = (base::TimeTicks::Now() - sent_at).InSecondsF();
  if (outcome.ok && test.reads.Run(outcome.body)) {
    result->outcome = mojom::TestOutcome::kPassed;
  } else if (outcome.ok) {
    result->outcome = mojom::TestOutcome::kUnusable;
  } else if (outcome.net_error != net::OK) {
    result->outcome = mojom::TestOutcome::kNoResponse;
    result->net_error = net::ErrorToShortString(outcome.net_error);
  } else {
    result->outcome = mojom::TestOutcome::kHttpError;
    result->provider_message = ProviderMessage(outcome.body);
  }
  result->detail = base::StrCat(
      {outcome.net_error == net::OK
           ? "HTTP " + base::NumberToString(outcome.http_status)
           : net::ErrorToShortString(outcome.net_error),
       "\n", Readable(outcome.body), "\n\nPOST ", test.url.spec(), "\n",
       Readable(test.language ? RequestIn(test.language->format, *test.body,
                                          test.language->endpoint)
                                    .value_or(*test.body)
                              : *test.body)});
  std::move(callback).Run(std::move(result));
}

void UwProvidersHandler::GetSkills(GetSkillsCallback callback) {
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::MayBlock()},
      base::BindOnce(&FindAllSkills, UwSkillsFolder(), AgentsSkillsFolder()),
      base::BindOnce(&UwProvidersHandler::ReplySkills,
                     weak_factory_.GetWeakPtr(), std::move(callback)));
}

void UwProvidersHandler::ReplySkills(GetSkillsCallback callback,
                                     std::vector<Skill> skills) {
  const base::ListValue& off =
      profile_->GetPrefs()->GetList(kAgentDisabledSkillsPref);
  std::vector<mojom::SkillInfoPtr> infos;
  for (const Skill& skill : skills) {
    mojom::SkillSource source = mojom::SkillSource::kUw;
    switch (skill.source) {
      case Skill::Source::kUw:
        source = mojom::SkillSource::kUw;
        break;
      case Skill::Source::kAgents:
        source = mojom::SkillSource::kAgents;
        break;
      case Skill::Source::kChat:
        source = mojom::SkillSource::kChat;
        break;
    }
    infos.push_back(mojom::SkillInfo::New(
        skill.name, skill.description, source, !off.contains(skill.name),
        skill.for_agent, skill.for_user, skill.argument_hint));
  }
  std::move(callback).Run(std::move(infos));
}

void UwProvidersHandler::SetSkillOn(const std::string& name, bool on) {
  ScopedListPrefUpdate off(profile_->GetPrefs(), kAgentDisabledSkillsPref);
  off->EraseValue(base::Value(name));
  if (!on) {
    off->Append(name);
  }
}

void UwProvidersHandler::OpenSkillsFolder() {
  base::ThreadPool::PostTaskAndReply(
      FROM_HERE, {base::MayBlock(), base::TaskPriority::USER_BLOCKING},
      base::BindOnce(base::IgnoreResult(&base::CreateDirectory),
                     UwSkillsFolder()),
      base::BindOnce(
          [](base::WeakPtr<UwProvidersHandler> self) {
            if (self) {
              platform_util::OpenItem(self->profile_, UwSkillsFolder(),
                                      platform_util::OPEN_FOLDER, {});
            }
          },
          weak_factory_.GetWeakPtr()));
}

void UwProvidersHandler::ShowAiPage(const std::string& section) {
  tabs::TabInterface* tab =
      contents_ ? tabs::TabInterface::MaybeGetFromContents(contents_) : nullptr;
  if (!tab) {
    return;
  }
  NavigateParams params = GetSingletonTabNavigateParams(
      tab->GetBrowserWindowInterface(),
      GURL(base::StrCat(
          {content::kChromeUIScheme, "://", kAiHost, "/", section})));
  // Any uw://ai tab will do; it goes to the section.
  params.path_behavior = NavigateParams::IGNORE_AND_NAVIGATE;
  ShowSingletonTabOverwritingNTP(&params);
}

void UwProvidersHandler::ShowChat() {
  tabs::TabInterface* tab =
      contents_ ? tabs::TabInterface::MaybeGetFromContents(contents_) : nullptr;
  if (!tab) {
    return;
  }
  BrowserWindowInterface* window = tab->GetBrowserWindowInterface();
  if (tabs::TabInterface* chat = LastChatTab(window)) {
    GoToTab(chat);
  } else {
    OpenChatTab(window);
  }
}

void UwProvidersHandler::OpenChat(const std::string& id) {
  if (!ChatFile::IsChatId(id)) {
    return;
  }
  std::vector<tabs::TabInterface*> showing = TabsShowingChat(profile_, id);
  if (!showing.empty()) {
    GoToTab(showing[0]);
    return;
  }
  tabs::TabInterface* tab =
      contents_ ? tabs::TabInterface::MaybeGetFromContents(contents_) : nullptr;
  if (!tab) {
    return;
  }
  NavigateParams params(tab->GetBrowserWindowInterface(), ChatUrl(id),
                        ui::PAGE_TRANSITION_AUTO_TOPLEVEL);
  params.disposition = WindowOpenDisposition::NEW_FOREGROUND_TAB;
  Navigate(&params);
}

void UwProvidersHandler::GetMemories(const std::string& query,
                                     GetMemoriesCallback callback) {
  MemoryStore* memory = MemoryStore::For(profile_);
  if (!memory) {
    std::move(callback).Run({}, 0, false);
    return;
  }
  // Every memory, as the page lists them all.
  constexpr size_t kAll = 100000;
  memory->Search(
      MemorySearchWords(query), kAll,
      base::BindOnce(
          [](base::WeakPtr<UwProvidersHandler> handler, MemoryStore* memory,
             GetMemoriesCallback callback, std::vector<Memory> found) {
            if (!handler) {
              return;
            }
            memory->Count(base::BindOnce(&UwProvidersHandler::ReplyMemories,
                                         handler, std::move(callback),
                                         std::move(found)));
          },
          weak_factory_.GetWeakPtr(), memory, std::move(callback)));
}

void UwProvidersHandler::ReplyMemories(GetMemoriesCallback callback,
                                       std::vector<Memory> memories,
                                       size_t total) {
  std::map<std::string, std::string> titles;
  if (ChatIndex* index = ChatIndex::For(profile_)) {
    for (const ChatIndex::Entry& entry : index->Entries()) {
      titles[entry.id] = entry.title;
    }
  }
  std::vector<mojom::MemoryInfoPtr> listed;
  for (Memory& memory : memories) {
    auto info = mojom::MemoryInfo::New();
    info->id = memory.id;
    info->text = std::move(memory.text);
    info->changed = memory.changed.InMillisecondsFSinceUnixEpoch();
    if (auto title = titles.find(memory.chat); title != titles.end()) {
      info->chat_title = title->second;
    }
    info->chat = std::move(memory.chat);
    listed.push_back(std::move(info));
  }
  std::move(callback).Run(std::move(listed), static_cast<int32_t>(total),
                          profile_->GetPrefs()->GetBoolean(kAgentMemoryPref));
}

void UwProvidersHandler::EditMemory(int64_t id,
                                    const std::string& text,
                                    EditMemoryCallback callback) {
  MemoryStore* memory = MemoryStore::For(profile_);
  if (!memory) {
    std::move(callback).Run("This profile keeps no memories.");
    return;
  }
  memory->Edit(id, text,
               base::BindOnce(
                   [](EditMemoryCallback callback,
                      base::expected<Memory, std::string> edited) {
                     std::move(callback).Run(
                         edited.has_value() ? std::string() : edited.error());
                   },
                   std::move(callback)));
}

void UwProvidersHandler::ForgetMemory(int64_t id,
                                      ForgetMemoryCallback callback) {
  MemoryStore* memory = MemoryStore::For(profile_);
  if (!memory) {
    std::move(callback).Run();
    return;
  }
  memory->Forget(id,
                 base::IgnoreArgs<std::optional<Memory>>(std::move(callback)));
}

void UwProvidersHandler::ForgetAllMemories(ForgetAllMemoriesCallback callback) {
  MemoryStore* memory = MemoryStore::For(profile_);
  if (!memory) {
    std::move(callback).Run();
    return;
  }
  memory->ForgetAll(std::move(callback));
}

void UwProvidersHandler::SetMemoryOn(bool on) {
  profile_->GetPrefs()->SetBoolean(kAgentMemoryPref, on);
}

void UwProvidersHandler::WaitForMemoryChange(
    WaitForMemoryChangeCallback callback) {
  waiting_for_memory_.push_back(std::move(callback));
}

void UwProvidersHandler::OnMemoriesChanged() {
  for (WaitForMemoryChangeCallback& callback :
       std::exchange(waiting_for_memory_, {})) {
    std::move(callback).Run();
  }
}

namespace {

mojom::ConnectorState StateFor(Connectors::State state) {
  switch (state) {
    case Connectors::State::kOff:
      return mojom::ConnectorState::kOff;
    case Connectors::State::kStarting:
      return mojom::ConnectorState::kStarting;
    case Connectors::State::kReady:
      return mojom::ConnectorState::kReady;
    case Connectors::State::kNeedsSignIn:
      return mojom::ConnectorState::kNeedsSignIn;
    case Connectors::State::kSigningIn:
      return mojom::ConnectorState::kSigningIn;
    case Connectors::State::kCantSignIn:
      return mojom::ConnectorState::kCantSignIn;
    case Connectors::State::kFailed:
      return mojom::ConnectorState::kFailed;
  }
}

std::vector<std::string> SecretNames(const ConnectorConfig& config) {
  std::vector<std::string> names;
  for (const auto& values : {config.env, config.headers}) {
    for (const auto& [key, value] : values) {
      names.push_back(key);
    }
  }
  return names;
}

std::string AddressOf(const ConnectorConfig& config) {
  if (config.remote()) {
    return config.url;
  }
  std::string address = config.command;
  for (const std::string& arg : config.args) {
    base::StrAppend(&address, {" ", arg});
  }
  return address;
}

}  // namespace

void UwProvidersHandler::GetConnectors(GetConnectorsCallback callback) {
  Connectors::Get().Start();
  std::vector<mojom::ConnectorInfoPtr> connectors;
  for (const Connectors::Info& info : Connectors::Get().list()) {
    auto connector = mojom::ConnectorInfo::New();
    connector->name = info.config.name;
    connector->remote = info.config.remote();
    connector->address = AddressOf(info.config);
    connector->on = info.on;
    connector->state = StateFor(info.state);
    connector->error = info.error;
    connector->version = info.version;
    for (const mcp::Tool& tool : info.tools) {
      connector->tools.push_back(
          mojom::ConnectorTool::New(tool.name, tool.description, tool.read_only,
                                    !info.tools_off.contains(tool.name),
                                    info.always_allowed.contains(tool.name)));
    }
    for (const mcp::Prompt& prompt : info.prompts) {
      connector->prompts.push_back(
          mojom::ConnectorPrompt::New(prompt.name, prompt.description));
    }
    connector->secret_names = SecretNames(info.config);
    connector->log_lines = info.log_lines;
    connectors.push_back(std::move(connector));
  }
  std::move(callback).Run(std::move(connectors),
                          Connectors::Get().environment_problem());
}

void UwProvidersHandler::WaitForConnectorChange(
    WaitForConnectorChangeCallback callback) {
  waiting_for_change_.push_back(std::move(callback));
}

void UwProvidersHandler::OnConnectorsChanged() {
  // A connector added from the catalog opens its sign-in once it has
  // started far enough to ask.
  std::vector<std::string> sign_in;
  for (auto name = sign_in_when_asked_.begin();
       name != sign_in_when_asked_.end();) {
    const Connectors::Info* info = Connectors::Get().Find(*name);
    if (info && info->state == Connectors::State::kStarting) {
      ++name;
      continue;
    }
    if (info && info->state == Connectors::State::kNeedsSignIn) {
      sign_in.push_back(*name);
    }
    name = sign_in_when_asked_.erase(name);
  }
  // Signing in changes the connectors again, which must wait until every
  // observer has heard of this change.
  for (std::string& name : sign_in) {
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(&UwProvidersHandler::SignInConnector,
                                  weak_factory_.GetWeakPtr(), std::move(name)));
  }
  OnCatalogChanged();
}

void UwProvidersHandler::OnCatalogChanged() {
  for (WaitForConnectorChangeCallback& callback :
       std::exchange(waiting_for_change_, {})) {
    std::move(callback).Run();
  }
}

namespace {

// The most entries one search shows.
constexpr size_t kSearchLimit = 30;

mojom::CatalogSource PageSource(CatalogSource source) {
  switch (source) {
    case CatalogSource::kRecommended:
      return mojom::CatalogSource::kRecommended;
    case CatalogSource::kClaudePlugin:
      return mojom::CatalogSource::kClaudePlugin;
    case CatalogSource::kOpenAiPlugin:
      return mojom::CatalogSource::kOpenAiPlugin;
    case CatalogSource::kRegistry:
      return mojom::CatalogSource::kRegistry;
  }
}

mojom::CatalogEntryPtr PageEntry(const CatalogEntry& entry) {
  auto page = mojom::CatalogEntry::New();
  page->id = entry.id;
  page->source = PageSource(entry.source);
  page->name = entry.name;
  page->title = entry.title;
  page->description = entry.description;
  page->website = entry.website;
  page->url = entry.url;
  for (const CatalogField& field : entry.fields) {
    page->fields.push_back(mojom::CatalogField::New(
        field.name, field.description, field.required, field.secret,
        field.value));
  }
  page->added = std::ranges::any_of(
      Connectors::Get().list(), [&](const Connectors::Info& info) {
        return info.config.remote() && SameServer(info.config.url, entry.url);
      });
  return page;
}

}  // namespace

void UwProvidersHandler::SearchCatalog(const std::string& query,
                                       SearchCatalogCallback callback) {
  Connectors::Get().Start();
  ConnectorCatalogStore& store = ConnectorCatalogStore::Get();
  store.Open();
  auto answer = mojom::CatalogAnswer::New();
  for (const CatalogEntry& entry :
       store.catalog().Search(query, kSearchLimit)) {
    answer->entries.push_back(PageEntry(entry));
  }
  answer->refreshed =
      store.catalog().refreshed.is_null()
          ? 0
          : store.catalog().refreshed.InMillisecondsFSinceUnixEpoch();
  answer->refreshing = store.refreshing();
  std::move(callback).Run(std::move(answer));
}

void UwProvidersHandler::AddFromCatalog(
    const std::string& id,
    const base::flat_map<std::string, std::string>& values,
    AddFromCatalogCallback callback) {
  std::optional<CatalogEntry> entry =
      ConnectorCatalogStore::Get().catalog().Find(id);
  if (!entry) {
    std::move(callback).Run("uw no longer lists that connector. Search again.");
    return;
  }
  std::string name = entry->name;
  for (int next = 2; Connectors::Get().Find(name); ++next) {
    name = base::StrCat(
        {entry->name.substr(0, 60), "-", base::NumberToString(next)});
  }
  base::expected<ConnectorConfig, std::string> config = ConnectorFor(
      *entry, name, std::map<std::string, std::string>(values.begin(),
                                                       values.end()));
  if (!config.has_value()) {
    std::move(callback).Run(config.error());
    return;
  }
  std::vector<ConnectorConfig> added;
  added.push_back(std::move(config).value());
  base::expected<void, std::string> result =
      Connectors::Get().Add(std::move(added));
  if (!result.has_value()) {
    std::move(callback).Run(result.error());
    return;
  }
  sign_in_when_asked_.insert(name);
  std::move(callback).Run(std::string());
}

void UwProvidersHandler::ReadConnectors(const std::string& json,
                                        ReadConnectorsCallback callback) {
  base::expected<std::vector<ConnectorConfig>, std::string> read =
      ParseConnectors(json);
  std::vector<mojom::NewConnectorPtr> connectors;
  if (!read.has_value()) {
    std::move(callback).Run(std::move(connectors), read.error());
    return;
  }
  for (const ConnectorConfig& config : *read) {
    connectors.push_back(mojom::NewConnector::New(
        config.name, config.remote(), AddressOf(config), SecretNames(config)));
  }
  std::move(callback).Run(std::move(connectors), std::string());
}

void UwProvidersHandler::AddConnectors(const std::string& json,
                                       AddConnectorsCallback callback) {
  base::expected<std::vector<ConnectorConfig>, std::string> read =
      ParseConnectors(json);
  if (!read.has_value()) {
    std::move(callback).Run(read.error());
    return;
  }
  Connectors::Get().AddPasted(std::move(read).value(), std::move(callback));
}

void UwProvidersHandler::RemoveConnector(const std::string& name) {
  Connectors::Get().Remove(name);
}

void UwProvidersHandler::SetConnectorOn(const std::string& name, bool on) {
  Connectors::Get().SetOn(name, on);
}

void UwProvidersHandler::SetConnectorToolOn(const std::string& name,
                                            const std::string& tool,
                                            bool on) {
  Connectors::Get().SetToolOn(name, tool, on);
}

void UwProvidersHandler::SetToolAlwaysAllowed(const std::string& name,
                                              const std::string& tool,
                                              bool allowed) {
  Connectors::Get().SetAlwaysAllowed(name, tool, allowed);
}

void UwProvidersHandler::RestartConnector(const std::string& name) {
  Connectors::Get().Restart(name);
}

void UwProvidersHandler::SignInConnector(const std::string& name) {
  Connectors::Get().SignIn(
      name,
      base::BindOnce(
          [](base::WeakPtr<UwProvidersHandler> self,
             const GURL& url) -> content::WebContents* {
            tabs::TabInterface* tab =
                self && self->contents_
                    ? tabs::TabInterface::MaybeGetFromContents(self->contents_)
                    : nullptr;
            if (!tab) {
              return nullptr;
            }
            NavigateParams params(tab->GetBrowserWindowInterface(), url,
                                  ui::PAGE_TRANSITION_LINK);
            params.disposition = WindowOpenDisposition::NEW_FOREGROUND_TAB;
            Navigate(&params);
            return params.navigated_or_inserted_contents.get();
          },
          weak_factory_.GetWeakPtr()));
}

void UwProvidersHandler::CancelSignIn(const std::string& name) {
  Connectors::Get().CancelSignIn(name);
}

void UwProvidersHandler::SignOutConnector(const std::string& name) {
  Connectors::Get().SignOut(name);
}

void UwProvidersHandler::OpenConnectorInTerminal(const std::string& name) {
  tabs::TabInterface* tab =
      contents_ ? tabs::TabInterface::MaybeGetFromContents(contents_) : nullptr;
  const std::string command = Connectors::Get().CommandLine(name);
  if (!tab || command.empty()) {
    return;
  }
  base::EnvironmentMap environment;
  for (const auto& [key, value] : Connectors::Get().Environment(name)) {
    environment[key] = value;
  }
  OpenTerminalTabRunning(tab->GetBrowserWindowInterface(), command,
                         std::move(environment));
}

void UwProvidersHandler::ShowConnectorLog(const std::string& name) {
  tabs::TabInterface* tab =
      contents_ ? tabs::TabInterface::MaybeGetFromContents(contents_) : nullptr;
  const Connectors::Info* info = Connectors::Get().Find(name);
  if (!tab || !info || info->config.remote()) {
    return;
  }
  NavigateParams params(tab->GetBrowserWindowInterface(),
                        net::FilePathToFileURL(Connectors::Get().LogFile(name)),
                        ui::PAGE_TRANSITION_LINK);
  params.disposition = WindowOpenDisposition::NEW_FOREGROUND_TAB;
  Navigate(&params);
}

}  // namespace uw
