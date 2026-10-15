// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/browser/ui/webui/uw_providers/uw_providers_handler.h"

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "base/containers/fixed_flat_set.h"
#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/values.h"
#include "chrome/browser/profiles/profile.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/storage_partition.h"
#include "mojo/public/cpp/bindings/callback_helpers.h"
#include "net/base/net_errors.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"
#include "url/gurl.h"
#include "uw/browser/ai_providers/api_key_store.h"
#include "uw/browser/ai_providers/models.h"
#include "uw/browser/tab_organization/ai_connection.h"
#include "uw/components/agent/agent_protocol.h"
#include "uw/components/ai_providers/formats.h"
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
constexpr size_t kMaxProviderMessageBytes = 200;
constexpr char kHiddenKey[] = "<api key>";

// Parts of model names that are plainly not for chat.
constexpr auto kNotForChat = base::MakeFixedFlatSet<std::string_view>(
    {"embed", "tts", "whisper", "dall-e", "image", "moderation", "transcribe",
     "speech", "rerank"});

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
    provider.address = input.address;
    provider.format = static_cast<ProviderFormat>(input.format);
  }
  return provider;
}

RemoteModelClient::KeyHeader KeyHeaderFor(const Provider& provider) {
  return provider.format == ProviderFormat::kAnthropicMessages
             ? RemoteModelClient::KeyHeader::kAnthropic
             : RemoteModelClient::KeyHeader::kBearer;
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
  return choice ? mojom::ModelChoice::New(choice->provider_id, choice->model,
                                          choice->tested)
                : nullptr;
}

bool ServesSlot(const Provider& provider, ModelSlot slot) {
  return slot == ModelSlot::kDecision ? DecisionFormatOf(provider).has_value()
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

// The provider's own words about an error, when its answer has any. OpenAI
// and DeepSeek send {"error": {"message": ...}}, TypeSafe sends
// {"detail": {"message": ...}}, and Ollama sends {"error": "..."}.
std::string ProviderMessage(const std::string& body) {
  const std::optional<base::DictValue> parsed =
      base::JSONReader::ReadDict(body, base::JSON_PARSE_RFC);
  if (!parsed) {
    return std::string();
  }
  const std::string* message = nullptr;
  for (const char* field : {"error", "detail", "message"}) {
    const base::DictValue* nested = parsed->FindDict(field);
    if (!message) {
      message =
          nested ? nested->FindString("message") : parsed->FindString(field);
    }
  }
  return message ? std::string(base::TruncateUTF8ToByteSize(
                       *message, kMaxProviderMessageBytes))
                 : std::string();
}

mojom::ModelListPtr ModelListFrom(RemoteModelClient::Outcome outcome) {
  auto list = mojom::ModelList::New();
  const std::optional<std::vector<std::u16string>> models =
      outcome.ok ? ParseModelList(outcome.body) : std::nullopt;
  if (!models) {
    list->error =
        outcome.net_error != net::OK
            ? net::ErrorToShortString(outcome.net_error)
            : base::StrCat({"HTTP ", base::NumberToString(outcome.http_status),
                            " ", ProviderMessage(outcome.body)});
    base::TrimWhitespaceASCII(list->error, base::TRIM_TRAILING, &list->error);
    return list;
  }
  list->ok = true;
  for (const std::u16string& model : *models) {
    const std::string name = base::UTF16ToUTF8(model);
    const std::string lower = base::ToLowerASCII(name);
    if (std::ranges::none_of(kNotForChat, [&](std::string_view part) {
          return lower.find(part) != std::string::npos;
        })) {
      list->models.push_back(name);
    }
  }
  return list;
}

// Sent to the slot's model as automatic organization reaches it. A language
// model passes when it answers with text, a decision model when it answers
// with a choice.
UwProvidersHandler::ModelTest TestFor(ModelSlot slot,
                                      const PrefService& prefs) {
  UwProvidersHandler::ModelTest test;
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
    mojo::PendingReceiver<mojom::ProvidersHandler> receiver)
    : profile_(profile), receiver_(this, std::move(receiver)) {}

UwProvidersHandler::~UwProvidersHandler() = default;

void UwProvidersHandler::GetState(GetStateCallback callback) {
  const PrefService& prefs = *profile_->GetPrefs();
  auto state = mojom::ProvidersState::New();
  for (const Provider& provider : ReadProviders(prefs)) {
    state->providers.push_back(PageProvider(provider));
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
  if (!provider || profile_->IsOffTheRecord() ||
      !IsValidProviderEndpoint(provider->address)) {
    return;
  }
  Send(std::make_unique<RemoteModelClient>(
           LoaderFactory(), ModelsUrl(provider->address), StoredKey(*provider),
           std::string(), kProviderTimeout, KeyHeaderFor(*provider)),
       /*post=*/false,
       base::BindOnce(&ModelListFrom).Then(std::move(callback)));
}

void UwProvidersHandler::ListModelsAt(mojom::NewProviderPtr input,
                                      ListModelsAtCallback callback) {
  callback = mojo::WrapCallbackWithDefaultInvokeIfNotRun(
      std::move(callback), mojom::ModelList::New());
  const Provider provider = ProviderFrom(*input);
  if (profile_->IsOffTheRecord() ||
      !IsValidProviderEndpoint(provider.address)) {
    return;
  }
  Send(std::make_unique<RemoteModelClient>(
           LoaderFactory(), ModelsUrl(provider.address), input->api_key,
           std::string(), kProviderTimeout, KeyHeaderFor(provider)),
       /*post=*/false,
       base::BindOnce(&ModelListFrom).Then(std::move(callback)));
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
  }
  std::move(callback).Run();
}

void UwProvidersHandler::RemoveProvider(const std::string& provider_id,
                                        RemoveProviderCallback callback) {
  ApiKeyStore().DeleteKey(KeyAccountFor(provider_id));
  uw::RemoveProvider(*profile_->GetPrefs(), provider_id);
  std::move(callback).Run();
}

void UwProvidersHandler::PickModel(mojom::ModelSlot page_slot,
                                   const std::string& provider_id,
                                   const std::string& model,
                                   PickModelCallback callback) {
  PrefService& prefs = *profile_->GetPrefs();
  const ModelSlot slot = SlotFrom(page_slot);
  const std::optional<Provider> provider = FindProvider(prefs, provider_id);
  if (provider && ServesSlot(*provider, slot) && !model.empty()) {
    SetChoice(prefs, slot, provider_id, model);
  }
  std::move(callback).Run();
}

void UwProvidersHandler::ClearModel(mojom::ModelSlot slot,
                                    ClearModelCallback callback) {
  ClearChoice(*profile_->GetPrefs(), SlotFrom(slot));
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
          ? RemoteModelClient::ForLanguageModel(
                LoaderFactory(), test.language->endpoint, test.language->format,
                key, *test.body, kProviderTimeout)
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
    MarkTested(*profile_->GetPrefs(), slot, choice);
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

}  // namespace uw
