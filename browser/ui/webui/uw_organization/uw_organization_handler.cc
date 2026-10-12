// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/browser/ui/webui/uw_organization/uw_organization_handler.h"

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "base/functional/callback.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"
#include "base/strings/utf_string_conversions.h"
#include "base/time/time.h"
#include "base/values.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/themes/theme_service.h"
#include "chrome/browser/themes/theme_service_factory.h"
#include "chrome/grit/generated_resources.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/storage_partition.h"
#include "content/public/browser/web_ui_data_source.h"
#include "mojo/public/cpp/bindings/callback_helpers.h"
#include "net/base/net_errors.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/mojom/themes.mojom.h"
#include "ui/base/webui/web_ui_util.h"
#include "url/gurl.h"
#include "uw/browser/tab_organization/ai_connection.h"
#include "uw/browser/tab_organization/api_key_store.h"
#include "uw/components/look/tint.h"

namespace uw {
namespace {

constexpr size_t kMaxDetailBytes = 6000;
constexpr size_t kMaxProviderMessageBytes = 200;
constexpr char kHiddenKey[] = "<api key>";

ConnectionMode ModeFromInt(int32_t mode) {
  return mode == static_cast<int32_t>(ConnectionMode::kOpenAiCompatible)
             ? ConnectionMode::kOpenAiCompatible
             : ConnectionMode::kOllama;
}

DecisionProvider ProviderFromInt(int32_t provider) {
  switch (provider) {
    case static_cast<int32_t>(DecisionProvider::kJev):
      return DecisionProvider::kJev;
    case static_cast<int32_t>(DecisionProvider::kOpenAi):
      return DecisionProvider::kOpenAi;
    case static_cast<int32_t>(DecisionProvider::kOpenAiCompatible):
      return DecisionProvider::kOpenAiCompatible;
    default:
      return DecisionProvider::kNone;
  }
}

// Stores `typed`, or keeps the stored key when nothing was typed. Removes the
// key when the connection takes none. Returns the account to record in prefs.
std::string StoreKey(const char* account,
                     bool needed,
                     const std::string& typed) {
  ApiKeyStore keys;
  if (!needed) {
    keys.DeleteKey(account);
    return std::string();
  }
  const bool stored = typed.empty() ? keys.GetKey(account).has_value()
                                    : keys.SetKey(account, typed);
  return stored ? account : std::string();
}

std::string StoredKey(const std::string& account) {
  return account.empty()
             ? std::string()
             : ApiKeyStore().GetKey(account).value_or(std::string());
}

mojom::ConnectionPtr ConnectionState(int32_t kind,
                                     const std::string& endpoint,
                                     const std::string& model,
                                     const std::string& key_account) {
  auto connection = mojom::Connection::New();
  connection->kind = kind;
  connection->endpoint = endpoint;
  connection->model = model;
  connection->has_key = !StoredKey(key_account).empty();
  return connection;
}

std::string Hex(SkColor color) {
  return base::StringPrintf("#%02X%02X%02X", SkColorGetR(color),
                            SkColorGetG(color), SkColorGetB(color));
}

std::optional<SkColor> ColorFromHex(const std::string& hex) {
  uint32_t rgb = 0;
  if (hex.size() != 7 || hex[0] != '#' ||
      !base::HexStringToUInt(hex.substr(1), &rgb)) {
    return std::nullopt;
  }
  return SkColorSetA(rgb, SK_AlphaOPAQUE);
}

BranchContext CheckContext() {
  BranchContext context;
  context.title = "Connectivity check";
  context.url = "about:blank";
  context.source_description = "setup validation";
  return context;
}

// The key a request to a connection as entered carries: the typed one, or
// the stored one when nothing was typed.
std::string KeyFor(const char* account,
                   bool takes_key,
                   const std::string& typed) {
  if (!takes_key) {
    return std::string();
  }
  return typed.empty() ? StoredKey(account) : typed;
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

}  // namespace

struct UwOrganizationHandler::ConnectionTest {
  std::string model;
  GURL url;
  // Kept until the answer arrives, to keep it out of what the page shows.
  std::string key;
  // Empty when the connection lacks a usable endpoint, a model, or a key.
  std::optional<std::string> body;
  base::RepeatingCallback<bool(const std::string&)> reads;
};

namespace {

using ConnectionTest = UwOrganizationHandler::ConnectionTest;

ConnectionTest DecisionTest(const DecisionModelConfig& config,
                            std::string key) {
  const DecisionFormat format = FormatFor(config.provider);
  ConnectionTest test;
  test.model = config.model;
  test.key = std::move(key);
  if (config.provider != DecisionProvider::kNone &&
      IsValidProviderEndpoint(config.endpoint) && !test.key.empty()) {
    test.url = DecisionUrl(config);
    test.body =
        BuildDecisionRequestBody(format, config.model, CheckContext(), {});
  }
  test.reads = base::BindRepeating(
      [](DecisionFormat format, const std::string& body) {
        return ParseDecisionResponse(format, body).has_value();
      },
      format);
  return test;
}

// `names_only` is true when a decision model does the deciding.
ConnectionTest LanguageTest(const AiConnectionConfig& config,
                            std::string key,
                            bool names_only) {
  ConnectionTest test;
  test.model = config.model;
  test.key = std::move(key);
  if (IsValidProviderEndpoint(config.endpoint) &&
      (config.mode == ConnectionMode::kOllama || !test.key.empty())) {
    test.url = ChatCompletionsUrl(config.endpoint);
    test.body =
        names_only ? BuildNamingRequestBody(config.model, CheckContext())
                   : BuildDecisionRequestBody(DecisionFormat::kChat,
                                              config.model, CheckContext(), {});
  }
  test.reads = base::BindRepeating(
      [](bool names_only, const std::string& body) {
        return names_only ? ParseNamingResponse(body).has_value()
                          : ParseDecisionResponse(DecisionFormat::kChat, body)
                                .has_value();
      },
      names_only);
  return test;
}

// Says in one sentence how a test went, and lists what came back and what
// was sent. The answer comes first because it is what explains a failure.
mojom::TestResultPtr Describe(const ConnectionTest& test,
                              const RemoteModelClient::Outcome& outcome,
                              base::TimeDelta took) {
  const std::u16string host = base::UTF8ToUTF16(test.url.host());
  const std::u16string model = base::UTF8ToUTF16(test.model);
  const std::string seconds = base::StringPrintf("%.1f", took.InSecondsF());
  const std::string error = net::ErrorToShortString(outcome.net_error);
  auto result = mojom::TestResult::New();
  std::u16string summary;
  if (outcome.net_error != net::OK) {
    summary = l10n_util::GetStringFUTF16(
        IDS_UW_SETTINGS_ORGANIZATION_TEST_NO_RESPONSE, host,
        base::UTF8ToUTF16(error));
  } else if (!outcome.ok) {
    summary = l10n_util::GetStringFUTF16(
        IDS_UW_SETTINGS_ORGANIZATION_TEST_HTTP_ERROR, host,
        base::NumberToString16(outcome.http_status));
    const std::string message = ProviderMessage(outcome.body);
    if (!message.empty()) {
      summary += u" " + base::UTF8ToUTF16(message);
    }
  } else if (!test.reads.Run(outcome.body)) {
    summary = l10n_util::GetStringFUTF16(
        IDS_UW_SETTINGS_ORGANIZATION_TEST_UNUSABLE, model);
  } else {
    result->ok = true;
    summary =
        l10n_util::GetStringFUTF16(IDS_UW_SETTINGS_ORGANIZATION_TEST_PASSED,
                                   model, base::UTF8ToUTF16(seconds));
  }
  result->summary = base::UTF16ToUTF8(summary);
  result->detail =
      base::StrCat({outcome.net_error == net::OK
                        ? "HTTP " + base::NumberToString(outcome.http_status)
                        : error,
                    " (", seconds, " s)\n", Readable(outcome.body), "\n\nPOST ",
                    test.url.spec(), "\n", Readable(*test.body)});
  return result;
}

}  // namespace

UwOrganizationHandler::UwOrganizationHandler(
    Profile* profile,
    mojo::PendingReceiver<mojom::OrganizationHandler> receiver)
    : profile_(profile), receiver_(this, std::move(receiver)) {}

UwOrganizationHandler::~UwOrganizationHandler() = default;

// static
void UwOrganizationHandler::AddLoadTimeData(
    content::WebUIDataSource* data_source) {
  static constexpr webui::LocalizedString kLocalizedStrings[] = {
      {"uwOrganizationPageTitle", IDS_UW_SETTINGS_ORGANIZATION_TITLE},
      {"uwOrganizationStatusOn", IDS_UW_SETTINGS_ORGANIZATION_STATUS_ON},
      {"uwOrganizationStatusOff", IDS_UW_SETTINGS_ORGANIZATION_STATUS_OFF},
      {"uwOrganizationStatusPaused",
       IDS_UW_SETTINGS_ORGANIZATION_STATUS_PAUSED},
      {"uwOrganizationStatusHint", IDS_UW_SETTINGS_ORGANIZATION_STATUS_HINT},
      {"uwOrganizationStatusDecides",
       IDS_UW_SETTINGS_ORGANIZATION_STATUS_DECIDES},
      {"uwOrganizationStatusNames", IDS_UW_SETTINGS_ORGANIZATION_STATUS_NAMES},
      {"uwOrganizationStatusDecidesAndNames",
       IDS_UW_SETTINGS_ORGANIZATION_STATUS_DECIDES_AND_NAMES},
      {"uwOrganizationDecisionTitle",
       IDS_UW_SETTINGS_ORGANIZATION_DECISION_TITLE},
      {"uwOrganizationDecisionNote",
       IDS_UW_SETTINGS_ORGANIZATION_DECISION_NOTE},
      {"uwOrganizationProvider", IDS_UW_SETTINGS_ORGANIZATION_PROVIDER},
      {"uwOrganizationProviderNone",
       IDS_UW_SETTINGS_ORGANIZATION_PROVIDER_NONE},
      {"uwOrganizationProviderJev", IDS_UW_SETTINGS_ORGANIZATION_PROVIDER_JEV},
      {"uwOrganizationProviderOpenai",
       IDS_UW_SETTINGS_ORGANIZATION_PROVIDER_OPENAI},
      {"uwOrganizationOpenaiCompatible",
       IDS_UW_SETTINGS_ORGANIZATION_OPENAI_COMPATIBLE},
      {"uwOrganizationLanguageTitle",
       IDS_UW_SETTINGS_ORGANIZATION_LANGUAGE_TITLE},
      {"uwOrganizationLanguageNote",
       IDS_UW_SETTINGS_ORGANIZATION_LANGUAGE_NOTE},
      {"uwOrganizationConnection", IDS_UW_SETTINGS_ORGANIZATION_CONNECTION},
      {"uwOrganizationOllama", IDS_UW_SETTINGS_ORGANIZATION_OLLAMA},
      {"uwOrganizationEndpoint", IDS_UW_SETTINGS_ORGANIZATION_ENDPOINT},
      {"uwOrganizationModel", IDS_UW_SETTINGS_ORGANIZATION_MODEL},
      {"uwOrganizationLoadModels", IDS_UW_SETTINGS_ORGANIZATION_LOAD_MODELS},
      {"uwOrganizationLoading", IDS_UW_SETTINGS_ORGANIZATION_LOADING},
      {"uwOrganizationModelsLoaded",
       IDS_UW_SETTINGS_ORGANIZATION_MODELS_LOADED},
      {"uwOrganizationModelsError", IDS_UW_SETTINGS_ORGANIZATION_MODELS_ERROR},
      {"uwOrganizationModelsEmpty", IDS_UW_SETTINGS_ORGANIZATION_MODELS_EMPTY},
      {"uwOrganizationKey", IDS_UW_SETTINGS_ORGANIZATION_KEY},
      {"uwOrganizationKeySaved", IDS_UW_SETTINGS_ORGANIZATION_KEY_SAVED},
      {"uwOrganizationNoKey", IDS_UW_SETTINGS_ORGANIZATION_NO_KEY},
      {"uwOrganizationSave", IDS_UW_SETTINGS_ORGANIZATION_SAVE},
      {"uwOrganizationChecking", IDS_UW_SETTINGS_ORGANIZATION_CHECKING},
      {"uwOrganizationValidated", IDS_UW_SETTINGS_ORGANIZATION_VALIDATED},
      {"uwOrganizationDecisionError",
       IDS_UW_SETTINGS_ORGANIZATION_DECISION_ERROR},
      {"uwOrganizationLanguageError",
       IDS_UW_SETTINGS_ORGANIZATION_LANGUAGE_ERROR},
      {"uwOrganizationNothingSet", IDS_UW_SETTINGS_ORGANIZATION_NOTHING_SET},
      {"uwOrganizationTest", IDS_UW_SETTINGS_ORGANIZATION_TEST},
      {"uwOrganizationTestDetails", IDS_UW_SETTINGS_ORGANIZATION_TEST_DETAILS},
      {"uwOrganizationScope", IDS_UW_SETTINGS_ORGANIZATION_SCOPE},
      {"uwOrganizationPauseTitle", IDS_UW_SETTINGS_ORGANIZATION_PAUSE_TITLE},
      {"uwOrganizationTintTitle", IDS_UW_SETTINGS_ORGANIZATION_TINT_TITLE},
      {"uwOrganizationTintColor", IDS_UW_SETTINGS_ORGANIZATION_TINT_COLOR},
      {"uwOrganizationTintSwatch", IDS_UW_SETTINGS_ORGANIZATION_TINT_SWATCH},
      {"uwOrganizationTintCustom", IDS_UW_SETTINGS_ORGANIZATION_TINT_CUSTOM},
      {"uwOrganizationTintDefault", IDS_UW_SETTINGS_ORGANIZATION_TINT_DEFAULT},
      {"uwOrganizationTintStatusDefault",
       IDS_UW_SETTINGS_ORGANIZATION_TINT_STATUS_DEFAULT},
      {"uwOrganizationTintStatusPicked",
       IDS_UW_SETTINGS_ORGANIZATION_TINT_STATUS_PICKED},
      {"uwOrganizationTintStatusAdjusted",
       IDS_UW_SETTINGS_ORGANIZATION_TINT_STATUS_ADJUSTED},
      {"uwOrganizationTintNote", IDS_UW_SETTINGS_ORGANIZATION_TINT_NOTE},
      {"uwOrganizationPause1Hour", IDS_UW_PAUSE_1HOUR},
      {"uwOrganizationPauseUntilLaunch", IDS_UW_PAUSE_UNTIL_LAUNCH},
      {"uwOrganizationPauseIndefinite", IDS_UW_PAUSE_INDEFINITE},
      {"uwOrganizationResume", IDS_UW_PAUSE_RESUME},
  };
  data_source->AddLocalizedStrings(kLocalizedStrings);
  data_source->AddString("uwOrganizationJevEndpoint", kJevDefaultEndpoint);
  data_source->AddString("uwOrganizationJevModel", kJevDefaultModel);
  data_source->AddString("uwOrganizationOpenaiEndpoint",
                         kOpenAiDefaultEndpoint);
  data_source->AddString("uwOrganizationOpenaiModel",
                         kOpenAiDecisionsDefaultModel);
  data_source->AddString("uwOrganizationOllamaEndpoint",
                         kOllamaDefaultEndpoint);
  data_source->AddString("uwOrganizationDefaultTint", Hex(kDefaultLightTint));
}

void UwOrganizationHandler::GetState(GetStateCallback callback) {
  const PrefService* prefs = profile_->GetPrefs();
  const DecisionModelConfig decision = ReadDecisionModelConfig(prefs);
  const AiConnectionConfig language = ReadAiConnectionConfig(prefs);
  auto state = mojom::State::New();
  state->decision =
      ConnectionState(static_cast<int32_t>(decision.provider),
                      decision.endpoint, decision.model, decision.key_account);
  state->language =
      ConnectionState(static_cast<int32_t>(language.mode), language.endpoint,
                      language.model, language.key_account);
  state->enabled = OrganizationReady(prefs);
  state->pause_mode = static_cast<int32_t>(ReadPauseState(prefs));
  std::move(callback).Run(std::move(state));
}

void UwOrganizationHandler::LoadModels(bool decision,
                                       mojom::ConnectionInputPtr connection,
                                       LoadModelsCallback callback) {
  callback = mojo::WrapCallbackWithDefaultInvokeIfNotRun(
      std::move(callback), mojom::ModelsResult::New());
  if (profile_->IsOffTheRecord() ||
      !IsValidProviderEndpoint(connection->endpoint)) {
    return;
  }
  const bool takes_key =
      decision || ModeFromInt(connection->kind) != ConnectionMode::kOllama;
  models_request_ =
      Client(ModelsUrl(connection->endpoint),
             KeyFor(decision ? kDecisionKeyAccount : kLanguageKeyAccount,
                    takes_key, connection->api_key),
             std::string());
  models_request_->SendGet(
      base::BindOnce(&UwOrganizationHandler::OnModelsLoaded,
                     weak_factory_.GetWeakPtr(), std::move(callback)));
}

void UwOrganizationHandler::OnModelsLoaded(LoadModelsCallback callback,
                                           RemoteModelClient::Outcome outcome) {
  models_request_.reset();
  const std::optional<std::vector<std::u16string>> models =
      outcome.ok ? ParseModelList(outcome.body) : std::nullopt;
  if (!models) {
    return;
  }
  auto result = mojom::ModelsResult::New();
  result->ok = true;
  for (const std::u16string& model : *models) {
    result->models.push_back(base::UTF16ToUTF8(model));
  }
  std::move(callback).Run(std::move(result));
}

void UwOrganizationHandler::TestConnection(bool decision,
                                           mojom::ConnectionInputPtr connection,
                                           bool names_only,
                                           TestConnectionCallback callback) {
  callback = mojo::WrapCallbackWithDefaultInvokeIfNotRun(
      std::move(callback), mojom::TestResult::New());
  if (profile_->IsOffTheRecord()) {
    return;
  }
  if (decision) {
    DecisionModelConfig config;
    config.provider = ProviderFromInt(connection->kind);
    config.endpoint = connection->endpoint;
    config.model = connection->model;
    RunTest(
        kDecisionSlot,
        DecisionTest(config, KeyFor(kDecisionKeyAccount,
                                    config.provider != DecisionProvider::kNone,
                                    connection->api_key)),
        std::move(callback));
    return;
  }
  AiConnectionConfig config;
  config.mode = ModeFromInt(connection->kind);
  config.endpoint = connection->endpoint;
  config.model = connection->model;
  RunTest(kLanguageSlot,
          LanguageTest(config,
                       KeyFor(kLanguageKeyAccount,
                              config.mode == ConnectionMode::kOpenAiCompatible,
                              connection->api_key),
                       names_only),
          std::move(callback));
}

void UwOrganizationHandler::Save(mojom::ConnectionInputPtr decision,
                                 mojom::ConnectionInputPtr language,
                                 SaveCallback callback) {
  for (std::unique_ptr<RemoteModelClient>& test : tests_) {
    test.reset();
  }
  save_result_ = mojom::SaveResult::New();
  save_callback_ = mojo::WrapCallbackWithDefaultInvokeIfNotRun(
      std::move(callback), mojom::SaveResult::New());
  PrefService* prefs = profile_->GetPrefs();
  prefs->SetBoolean(kOrganizationEnabledPref, false);
  if (profile_->IsOffTheRecord()) {
    std::move(save_callback_).Run(std::move(save_result_));
    return;
  }
  StoreDecision(*decision);
  StoreLanguage(*language);
  const DecisionModelConfig decides = ReadDecisionModelConfig(prefs);
  const AiConnectionConfig names = ReadAiConnectionConfig(prefs);
  const bool has_decision = decides.provider != DecisionProvider::kNone;
  const bool has_language = !names.model.empty();
  save_tests_left_ = (has_decision ? 1 : 0) + (has_language ? 1 : 0);
  if (save_tests_left_ == 0) {
    std::move(save_callback_).Run(std::move(save_result_));
    return;
  }
  if (has_decision) {
    RunTest(kDecisionSlot,
            DecisionTest(decides, StoredKey(decides.key_account)),
            base::BindOnce(&UwOrganizationHandler::OnSaveTested,
                           weak_factory_.GetWeakPtr(), kDecisionSlot));
  }
  if (has_language) {
    RunTest(kLanguageSlot,
            LanguageTest(names, StoredKey(names.key_account),
                         /*names_only=*/has_decision),
            base::BindOnce(&UwOrganizationHandler::OnSaveTested,
                           weak_factory_.GetWeakPtr(), kLanguageSlot));
  }
}

void UwOrganizationHandler::StoreDecision(const mojom::ConnectionInput& input) {
  PrefService* prefs = profile_->GetPrefs();
  const DecisionProvider provider = ProviderFromInt(input.kind);
  prefs->SetInteger(kDecisionProviderPref, static_cast<int>(provider));
  prefs->SetString(kDecisionEndpointPref, input.endpoint);
  prefs->SetString(kDecisionModelPref, input.model);
  prefs->SetString(
      kDecisionKeyAccountPref,
      StoreKey(kDecisionKeyAccount, provider != DecisionProvider::kNone,
               input.api_key));
}

void UwOrganizationHandler::StoreLanguage(const mojom::ConnectionInput& input) {
  PrefService* prefs = profile_->GetPrefs();
  const ConnectionMode mode = ModeFromInt(input.kind);
  prefs->SetInteger(kOrganizationModePref, static_cast<int>(mode));
  prefs->SetString(kOrganizationEndpointPref, input.endpoint);
  prefs->SetString(kOrganizationModelPref, input.model);
  prefs->SetString(
      kOrganizationKeyAccountPref,
      StoreKey(kLanguageKeyAccount, mode == ConnectionMode::kOpenAiCompatible,
               input.api_key));
}

void UwOrganizationHandler::RunTest(Slot slot,
                                    ConnectionTest test,
                                    TestConnectionCallback done) {
  tests_[slot].reset();
  if (!test.body) {
    auto result = mojom::TestResult::New();
    result->summary =
        l10n_util::GetStringUTF8(IDS_UW_SETTINGS_ORGANIZATION_TEST_INCOMPLETE);
    std::move(done).Run(std::move(result));
    return;
  }
  tests_[slot] = Client(test.url, test.key, *test.body);
  tests_[slot]->Send(base::BindOnce(
      &UwOrganizationHandler::OnTested, weak_factory_.GetWeakPtr(), slot,
      std::move(test), base::TimeTicks::Now(), std::move(done)));
}

void UwOrganizationHandler::OnTested(Slot slot,
                                     ConnectionTest test,
                                     base::TimeTicks sent_at,
                                     TestConnectionCallback done,
                                     RemoteModelClient::Outcome outcome) {
  tests_[slot].reset();
  if (!test.key.empty()) {
    base::ReplaceSubstringsAfterOffset(&outcome.body, 0, test.key, kHiddenKey);
  }
  std::move(done).Run(
      Describe(test, outcome, base::TimeTicks::Now() - sent_at));
}

void UwOrganizationHandler::OnSaveTested(Slot slot,
                                         mojom::TestResultPtr result) {
  (slot == kDecisionSlot ? save_result_->decision : save_result_->language) =
      std::move(result);
  if (--save_tests_left_ > 0) {
    return;
  }
  save_result_->enabled =
      (!save_result_->decision || save_result_->decision->ok) &&
      (!save_result_->language || save_result_->language->ok);
  profile_->GetPrefs()->SetBoolean(kOrganizationEnabledPref,
                                   save_result_->enabled);
  std::move(save_callback_).Run(std::move(save_result_));
}

std::unique_ptr<RemoteModelClient> UwOrganizationHandler::Client(
    const GURL& url,
    std::string api_key,
    std::string body) {
  return std::make_unique<RemoteModelClient>(
      profile_->GetDefaultStoragePartition()
          ->GetURLLoaderFactoryForBrowserProcess(),
      url, std::move(api_key), std::move(body), kProviderTimeout);
}

void UwOrganizationHandler::TurnOff(TurnOffCallback callback) {
  profile_->GetPrefs()->SetBoolean(kOrganizationEnabledPref, false);
  std::move(callback).Run();
}

void UwOrganizationHandler::SetPause(int32_t mode_value,
                                     SetPauseCallback callback) {
  PrefService* prefs = profile_->GetPrefs();
  switch (mode_value) {
    case static_cast<int32_t>(PauseMode::kTimed):
      SetPauseState(prefs, PauseMode::kTimed, base::Hours(1));
      break;
    case static_cast<int32_t>(PauseMode::kUntilLaunch):
      SetPauseState(prefs, PauseMode::kUntilLaunch);
      break;
    case static_cast<int32_t>(PauseMode::kIndefinite):
      SetPauseState(prefs, PauseMode::kIndefinite);
      break;
    default:
      SetPauseState(prefs, PauseMode::kOff);
      break;
  }
  auto result = mojom::PauseResult::New();
  result->pause_mode = static_cast<int32_t>(ReadPauseState(prefs));
  std::move(callback).Run(std::move(result));
}

void UwOrganizationHandler::GetTint(GetTintCallback callback) {
  std::move(callback).Run(CurrentTint());
}

void UwOrganizationHandler::SetTint(const std::string& color,
                                    SetTintCallback callback) {
  if (const std::optional<SkColor> picked = ColorFromHex(color)) {
    ThemeServiceFactory::GetForProfile(profile_)
        ->SetUserColorAndBrowserColorVariant(
            *picked, ui::mojom::BrowserColorVariant::kTonalSpot);
  }
  std::move(callback).Run(CurrentTint());
}

void UwOrganizationHandler::ClearTint(ClearTintCallback callback) {
  ThemeServiceFactory::GetForProfile(profile_)->SetUserColor(std::nullopt);
  std::move(callback).Run(CurrentTint());
}

mojom::TintStatePtr UwOrganizationHandler::CurrentTint() const {
  auto tint = mojom::TintState::New();
  if (const std::optional<SkColor> picked =
          ThemeServiceFactory::GetForProfile(profile_)->GetUserColor()) {
    tint->picked = Hex(*picked);
    tint->applied = Hex(TintFrom(*picked).start);
  }
  return tint;
}

}  // namespace uw
