// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/browser/ui/webui/uw_organization/uw_organization_handler.h"

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "base/check.h"
#include "base/functional/bind.h"
#include "base/strings/utf_string_conversions.h"
#include "base/time/time.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/grit/generated_resources.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/storage_partition.h"
#include "content/public/browser/web_ui_data_source.h"
#include "uw/browser/tab_organization/ai_connection.h"
#include "uw/browser/tab_organization/api_key_store.h"
#include "uw/browser/tab_organization/remote_model_client.h"
#include "uw/components/tab_organization/ai_protocol.h"

namespace uw {
namespace {

// Fixed Keychain account slot; the slot (not the secret) is unremarkable.
constexpr char kOrganizationAccount[] = "organization";
constexpr base::TimeDelta kRequestTimeout = base::Seconds(15);

ConnectionMode ModeFromInt(int32_t mode) {
  return mode == static_cast<int32_t>(ConnectionMode::kOpenAiCompatible)
             ? ConnectionMode::kOpenAiCompatible
             : ConnectionMode::kOllama;
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
  // Connection labels reuse the setup strings so the settings page and the
  // former dialog describe the same scope.
  static constexpr webui::LocalizedString kLocalizedStrings[] = {
      {"uwOrganizationPageTitle", IDS_UW_SETTINGS_ORGANIZATION_TITLE},
      {"uwOrganizationConnectionTitle", IDS_UW_AUTO_SETUP_TITLE},
      {"uwOrganizationModeLabel", IDS_UW_AUTO_SETUP_MODE},
      {"uwOrganizationModeOllama", IDS_UW_AUTO_SETUP_MODE_OLLAMA},
      {"uwOrganizationModeOpenAi", IDS_UW_AUTO_SETUP_MODE_OPENAI},
      {"uwOrganizationEndpointLabel", IDS_UW_AUTO_SETUP_ENDPOINT},
      {"uwOrganizationLoadModelsLabel", IDS_UW_AUTO_SETUP_LOAD_MODELS},
      {"uwOrganizationModelLabel", IDS_UW_AUTO_SETUP_MODEL},
      {"uwOrganizationModelEmpty", IDS_UW_AUTO_SETUP_MODEL_EMPTY},
      {"uwOrganizationModelsError", IDS_UW_AUTO_SETUP_MODELS_ERROR},
      {"uwOrganizationModelsEmpty", IDS_UW_AUTO_SETUP_MODELS_EMPTY},
      {"uwOrganizationKeyLabel", IDS_UW_AUTO_SETUP_KEY},
      {"uwOrganizationKeySaved", IDS_UW_SETTINGS_ORGANIZATION_KEY_SAVED},
      {"uwOrganizationNoKey", IDS_UW_SETTINGS_ORGANIZATION_NO_KEY},
      {"uwOrganizationScope", IDS_UW_AUTO_SCOPE},
      {"uwOrganizationSaveLabel", IDS_UW_SETTINGS_ORGANIZATION_SAVE},
      {"uwOrganizationSetupError", IDS_UW_AUTO_SETUP_ERROR},
      {"uwOrganizationEnabled", IDS_UW_AUTO_ENABLED},
      {"uwOrganizationChecking", IDS_UW_SETTINGS_ORGANIZATION_CHECKING},
      {"uwOrganizationLoading", IDS_UW_SETTINGS_ORGANIZATION_LOADING},
      {"uwOrganizationModelsLoaded",
       IDS_UW_SETTINGS_ORGANIZATION_MODELS_LOADED},
      {"uwOrganizationPauseTitle", IDS_UW_SETTINGS_ORGANIZATION_PAUSE_TITLE},
      {"uwOrganizationPause1Hour", IDS_UW_PAUSE_1HOUR},
      {"uwOrganizationPauseUntilLaunch", IDS_UW_PAUSE_UNTIL_LAUNCH},
      {"uwOrganizationPauseIndefinite", IDS_UW_PAUSE_INDEFINITE},
      {"uwOrganizationResume", IDS_UW_PAUSE_RESUME},
  };
  data_source->AddLocalizedStrings(kLocalizedStrings);
}

void UwOrganizationHandler::GetState(GetStateCallback callback) {
  auto state = mojom::ConnectionState::New();
  const AiConnectionConfig config =
      ReadAiConnectionConfig(profile_->GetPrefs());
  ApiKeyStore keys;
  state->mode = static_cast<int32_t>(config.mode);
  state->endpoint = config.endpoint;
  state->model = config.model;
  state->has_key = !config.key_account.empty() &&
                   keys.GetKey(config.key_account).has_value();
  state->enabled = config.organization_enabled;
  state->pause_mode =
      static_cast<int32_t>(ReadPauseState(profile_->GetPrefs()));
  std::move(callback).Run(std::move(state));
}

void UwOrganizationHandler::LoadModels(const std::string& endpoint,
                                       const std::string& api_key,
                                       LoadModelsCallback callback) {
  auto result = mojom::ModelsResult::New();
  if (profile_->IsOffTheRecord() || !IsValidProviderEndpoint(endpoint)) {
    result->ok = false;
    std::move(callback).Run(std::move(result));
    return;
  }
  models_request_ = std::make_unique<RemoteModelClient>(
      profile_->GetDefaultStoragePartition()
          ->GetURLLoaderFactoryForBrowserProcess(),
      ModelsUrl(endpoint), api_key, std::string(), kRequestTimeout);
  models_request_->Send(
      base::BindOnce(&UwOrganizationHandler::OnModelsLoaded,
                     weak_factory_.GetWeakPtr(), std::move(callback)));
}

void UwOrganizationHandler::OnModelsLoaded(
    LoadModelsCallback callback,
    RemoteModelClient::Outcome outcome) {
  models_request_.reset();
  auto result = mojom::ModelsResult::New();
  std::optional<std::vector<std::u16string>> models;
  if (outcome.ok) {
    models = ParseModelList(outcome.body);
  }
  if (!models) {
    result->ok = false;
    std::move(callback).Run(std::move(result));
    return;
  }
  result->ok = true;
  for (const auto& model : *models) {
    result->models.push_back(base::UTF16ToUTF8(model));
  }
  std::move(callback).Run(std::move(result));
}

void UwOrganizationHandler::Save(int32_t mode_value,
                                 const std::string& endpoint,
                                 const std::string& model,
                                 const std::string& key,
                                 SaveCallback callback) {
  auto fail = [](SaveCallback done) {
    auto result = mojom::SaveResult::New();
    result->ok = false;
    std::move(done).Run(std::move(result));
  };
  if (profile_->IsOffTheRecord()) {
    return fail(std::move(callback));
  }
  const ConnectionMode mode = ModeFromInt(mode_value);
  if (!IsValidProviderEndpoint(endpoint) || model.empty()) {
    return fail(std::move(callback));
  }
  PrefService* prefs = profile_->GetPrefs();
  ApiKeyStore keys;
  std::string api_key;
  std::string key_account;
  if (mode == ConnectionMode::kOpenAiCompatible) {
    if (!key.empty()) {
      if (!keys.SetKey(kOrganizationAccount, key)) {
        return fail(std::move(callback));
      }
    } else if (!keys.GetKey(kOrganizationAccount)) {
      // No typed key and no stored key to keep.
      return fail(std::move(callback));
    }
    key_account = kOrganizationAccount;
    api_key = key.empty() ? keys.GetKey(kOrganizationAccount).value_or("")
                          : key;
  } else {
    keys.DeleteKey(kOrganizationAccount);
  }
  prefs->SetInteger(kOrganizationModePref, static_cast<int>(mode));
  prefs->SetString(kOrganizationEndpointPref, endpoint);
  prefs->SetString(kOrganizationModelPref, model);
  prefs->SetString(kOrganizationKeyAccountPref, key_account);
  AiConnectionConfig pending{mode, endpoint, model, key_account,
                             /*organization_enabled=*/true};
  if (!CanOrganize(pending)) {
    return fail(std::move(callback));
  }
  BranchContext context;
  context.title = "Connectivity check";
  context.url = "about:blank";
  context.source_description = "setup validation";
  std::optional<std::string> body =
      BuildClassificationRequestBody(model, context, {});
  if (!body) {
    return fail(std::move(callback));
  }
  validation_request_ = std::make_unique<RemoteModelClient>(
      profile_->GetDefaultStoragePartition()
          ->GetURLLoaderFactoryForBrowserProcess(),
      ChatCompletionsUrl(endpoint), api_key, *body, kRequestTimeout);
  api_key.assign(api_key.size(), '\0');
  validation_request_->Send(
      base::BindOnce(&UwOrganizationHandler::OnValidationComplete,
                     weak_factory_.GetWeakPtr(), std::move(callback)));
}

void UwOrganizationHandler::OnValidationComplete(
    SaveCallback callback,
    RemoteModelClient::Outcome outcome) {
  validation_request_.reset();
  auto result = mojom::SaveResult::New();
  if (!outcome.ok || !ParseClassificationResponse(outcome.body)) {
    result->ok = false;
    std::move(callback).Run(std::move(result));
    return;
  }
  profile_->GetPrefs()->SetBoolean(kOrganizationEnabledPref, true);
  result->ok = true;
  std::move(callback).Run(std::move(result));
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
  result->pause_mode =
      static_cast<int32_t>(ReadPauseState(profile_->GetPrefs()));
  std::move(callback).Run(std::move(result));
}

}  // namespace uw
