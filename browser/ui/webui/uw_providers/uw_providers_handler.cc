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
#include "url/gurl.h"
#include "uw/browser/tab_organization/ai_connection.h"
#include "uw/browser/tab_organization/api_key_store.h"
#include "uw/components/agent/agent_protocol.h"
#include "uw/components/tab_organization/ai_protocol.h"

namespace uw {

struct UwProvidersHandler::ModelTest {
  GURL url;
  std::optional<std::string> body;
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

// A language model passes when it answers with text, a decision model when
// it answers with a choice. Until uw speaks Anthropic Messages and OpenAI
// Responses, every language model is reached over chat completions.
UwProvidersHandler::ModelTest TestFor(ModelSlot slot,
                                      const Provider& provider,
                                      const std::string& model) {
  UwProvidersHandler::ModelTest test;
  if (slot == ModelSlot::kDecision) {
    const DecisionFormat format =
        DecisionFormatOf(provider) == ProviderFormat::kSystemOne
            ? DecisionFormat::kSystemOne
            : DecisionFormat::kDecisions;
    std::string base = provider.address;
    base::TrimString(base, "/", &base);
    test.url =
        GURL(base + (format == DecisionFormat::kSystemOne ? "/systemone"
                                                          : "/decisions"));
    test.body = BuildDecisionRequestBody(format, model, CheckContext(), {});
    test.reads = base::BindRepeating(
        [](DecisionFormat format, const std::string& body) {
          return ParseDecisionResponse(format, body, /*has_tasks=*/false)
              .has_value();
        },
        format);
    return test;
  }
  base::ListValue messages;
  messages.Append(UserMessage("Say hello in one word."));
  test.url = ChatCompletionsUrl(provider.address);
  test.body = BuildAgentRequestBody(model, messages, base::ListValue());
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
  Send(ModelsUrl(provider->address), *provider, StoredKey(*provider),
       std::nullopt, base::BindOnce(&ModelListFrom).Then(std::move(callback)));
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
  Send(ModelsUrl(provider.address), provider, input->api_key, std::nullopt,
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
  const std::optional<Provider> provider =
      choice ? FindProvider(prefs, choice->provider_id) : std::nullopt;
  auto incomplete = mojom::ModelTestResult::New();
  incomplete->outcome = mojom::TestOutcome::kIncomplete;
  incomplete->model = choice ? choice->model : std::string();
  if (!provider || slot == ModelSlot::kAgent || profile_->IsOffTheRecord() ||
      !ServesSlot(*provider, slot) ||
      !IsValidProviderEndpoint(provider->address)) {
    std::move(callback).Run(std::move(incomplete));
    return;
  }
  ModelTest test = TestFor(slot, *provider, choice->model);
  if (!test.body) {
    std::move(callback).Run(std::move(incomplete));
    return;
  }
  std::string key = StoredKey(*provider);
  const GURL url = test.url;
  const std::optional<std::string> body = test.body;
  Send(url, *provider, key, body,
       base::BindOnce(&UwProvidersHandler::OnTested, weak_factory_.GetWeakPtr(),
                      slot, *choice, std::move(test), key,
                      base::TimeTicks::Now(), std::move(callback)));
}

void UwProvidersHandler::Send(const GURL& url,
                              const Provider& provider,
                              std::string key,
                              std::optional<std::string> body,
                              RemoteModelClient::ResultCallback done) {
  const int request = next_request_++;
  auto client = std::make_unique<RemoteModelClient>(
      profile_->GetDefaultStoragePartition()
          ->GetURLLoaderFactoryForBrowserProcess(),
      url, std::move(key), body.value_or(std::string()), kProviderTimeout,
      KeyHeaderFor(provider));
  RemoteModelClient* sender = client.get();
  requests_[request] = std::move(client);
  auto answered =
      base::BindOnce(&UwProvidersHandler::OnAnswered,
                     weak_factory_.GetWeakPtr(), request, std::move(done));
  if (body) {
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
  result->detail =
      base::StrCat({outcome.net_error == net::OK
                        ? "HTTP " + base::NumberToString(outcome.http_status)
                        : net::ErrorToShortString(outcome.net_error),
                    "\n", Readable(outcome.body), "\n\nPOST ", test.url.spec(),
                    "\n", Readable(*test.body)});
  std::move(callback).Run(std::move(result));
}

}  // namespace uw
