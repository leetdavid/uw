// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/browser/ui/webui/uw_organization/uw_organization_handler.h"

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "base/strings/utf_string_conversions.h"
#include "base/time/time.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/grit/generated_resources.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/storage_partition.h"
#include "content/public/browser/web_ui_data_source.h"
#include "mojo/public/cpp/bindings/callback_helpers.h"
#include "ui/base/webui/web_ui_util.h"
#include "url/gurl.h"
#include "uw/browser/tab_organization/ai_connection.h"
#include "uw/browser/tab_organization/api_key_store.h"

namespace uw {
namespace {

constexpr base::TimeDelta kRequestTimeout = base::Seconds(15);

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

BranchContext CheckContext() {
  BranchContext context;
  context.title = "Connectivity check";
  context.url = "about:blank";
  context.source_description = "setup validation";
  return context;
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
      {"uwOrganizationScope", IDS_UW_SETTINGS_ORGANIZATION_SCOPE},
      {"uwOrganizationPauseTitle", IDS_UW_SETTINGS_ORGANIZATION_PAUSE_TITLE},
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
  std::string key = connection->api_key;
  if (takes_key && key.empty()) {
    key = StoredKey(decision ? kDecisionKeyAccount : kLanguageKeyAccount);
  }
  models_request_ =
      Client(ModelsUrl(connection->endpoint),
             takes_key ? std::move(key) : std::string(), std::string());
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

void UwOrganizationHandler::Save(mojom::ConnectionInputPtr decision,
                                 mojom::ConnectionInputPtr language,
                                 SaveCallback callback) {
  decision_check_.reset();
  language_check_.reset();
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
  const bool decides =
      ReadDecisionModelConfig(prefs).provider != DecisionProvider::kNone;
  const bool has_language = !ReadAiConnectionConfig(prefs).model.empty();
  if (!decides && !has_language) {
    std::move(save_callback_).Run(std::move(save_result_));
    return;
  }
  if (decides) {
    CheckDecision();
  }
  if (has_language) {
    CheckLanguage(/*names_only=*/decides);
  }
  FinishSaveWhenChecked();
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

void UwOrganizationHandler::CheckDecision() {
  const DecisionModelConfig config =
      ReadDecisionModelConfig(profile_->GetPrefs());
  const DecisionFormat format = FormatFor(config.provider);
  std::optional<std::string> body =
      IsComplete(config)
          ? BuildDecisionRequestBody(format, config.model, CheckContext(), {})
          : std::nullopt;
  if (!body) {
    save_result_->decision_failed = true;
    return;
  }
  decision_check_ = Client(DecisionUrl(config), StoredKey(config.key_account),
                           std::move(*body));
  decision_check_->Send(
      base::BindOnce(&UwOrganizationHandler::OnDecisionChecked,
                     weak_factory_.GetWeakPtr(), format));
}

void UwOrganizationHandler::CheckLanguage(bool names_only) {
  const AiConnectionConfig config =
      ReadAiConnectionConfig(profile_->GetPrefs());
  std::optional<std::string> body;
  if (IsComplete(config)) {
    body = names_only
               ? BuildNamingRequestBody(config.model, CheckContext())
               : BuildDecisionRequestBody(DecisionFormat::kChat, config.model,
                                          CheckContext(), {});
  }
  if (!body) {
    save_result_->language_failed = true;
    return;
  }
  language_check_ = Client(ChatCompletionsUrl(config.endpoint),
                           StoredKey(config.key_account), std::move(*body));
  language_check_->Send(
      base::BindOnce(&UwOrganizationHandler::OnLanguageChecked,
                     weak_factory_.GetWeakPtr(), names_only));
}

void UwOrganizationHandler::OnDecisionChecked(
    DecisionFormat format,
    RemoteModelClient::Outcome outcome) {
  decision_check_.reset();
  save_result_->decision_failed =
      !outcome.ok || !ParseDecisionResponse(format, outcome.body);
  FinishSaveWhenChecked();
}

void UwOrganizationHandler::OnLanguageChecked(
    bool names_only,
    RemoteModelClient::Outcome outcome) {
  language_check_.reset();
  const bool valid =
      outcome.ok &&
      (names_only ? ParseNamingResponse(outcome.body).has_value()
                  : ParseDecisionResponse(DecisionFormat::kChat, outcome.body)
                        .has_value());
  save_result_->language_failed = !valid;
  FinishSaveWhenChecked();
}

void UwOrganizationHandler::FinishSaveWhenChecked() {
  if (decision_check_ || language_check_) {
    return;
  }
  save_result_->enabled =
      !save_result_->decision_failed && !save_result_->language_failed;
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
      url, std::move(api_key), std::move(body), kRequestTimeout);
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

}  // namespace uw
