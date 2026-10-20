// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/browser/ui/webui/uw_organization/uw_organization_handler.h"

#include <algorithm>
#include <optional>
#include <string>
#include <utility>

#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_window/public/profile_browser_collection.h"
#include "chrome/grit/generated_resources.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/web_ui_data_source.h"
#include "ui/base/webui/web_ui_util.h"
#include "uw/browser/ai_providers/models.h"
#include "uw/browser/tab_organization/ai_connection.h"
#include "uw/browser/tab_organization/commands.h"
#include "uw/browser/ui/webui/uw_command_bar/exchange_rates.h"
#include "uw/components/tab_organization/placement.h"

namespace uw {
namespace {

const char* MouseActionPref(mojom::MouseAction action) {
  switch (action) {
    case mojom::MouseAction::kOptionClickClosesBranch:
      return kOptionClickClosesBranchPref;
    case mojom::MouseAction::kTaskRowNewTab:
      return kTaskRowNewTabPref;
    case mojom::MouseAction::kDoubleClickOpensTab:
      return kDoubleClickOpensTabPref;
    case mojom::MouseAction::kMiddleClickClosesTask:
      return kMiddleClickClosesTaskPref;
  }
}

mojom::Waiting WaitingFor(const PrefService* prefs) {
  return IsComplete(ReadDecisionModelConfig(prefs)) ||
                 IsComplete(ReadAiConnectionConfig(prefs))
             ? mojom::Waiting::kNothing
             : mojom::Waiting::kNoModel;
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
      {"uwOrganizationStatusWaiting",
       IDS_UW_SETTINGS_ORGANIZATION_STATUS_WAITING},
      {"uwOrganizationStatusHint", IDS_UW_SETTINGS_ORGANIZATION_STATUS_HINT},
      {"uwOrganizationStatusDecides",
       IDS_UW_SETTINGS_ORGANIZATION_STATUS_DECIDES},
      {"uwOrganizationStatusNames", IDS_UW_SETTINGS_ORGANIZATION_STATUS_NAMES},
      {"uwOrganizationStatusDecidesAndNames",
       IDS_UW_SETTINGS_ORGANIZATION_STATUS_DECIDES_AND_NAMES},
      {"uwOrganizationWaitingNoModel",
       IDS_UW_SETTINGS_ORGANIZATION_WAITING_NO_MODEL},
      {"uwOrganizationProvidersTitle",
       IDS_UW_SETTINGS_ORGANIZATION_PROVIDERS_TITLE},
      {"uwOrganizationProvidersNote",
       IDS_UW_SETTINGS_ORGANIZATION_PROVIDERS_NOTE},
      {"uwOrganizationScope", IDS_UW_SETTINGS_ORGANIZATION_SCOPE},
      {"uwOrganizationThresholdMove",
       IDS_UW_SETTINGS_ORGANIZATION_THRESHOLD_MOVE},
      {"uwOrganizationThresholdNewTask",
       IDS_UW_SETTINGS_ORGANIZATION_THRESHOLD_NEW_TASK},
      {"uwOrganizationThresholdRename",
       IDS_UW_SETTINGS_ORGANIZATION_THRESHOLD_RENAME},
      {"uwOrganizationThresholdsNote",
       IDS_UW_SETTINGS_ORGANIZATION_THRESHOLDS_NOTE},
      {"uwOrganizationPauseTitle", IDS_UW_SETTINGS_ORGANIZATION_PAUSE_TITLE},
      {"uwOrganizationTheme", IDS_UW_SETTINGS_THEME},
      {"uwOrganizationThemeDescription", IDS_UW_SETTINGS_THEME_DESCRIPTION},
      {"uwCommandBarTitle", IDS_UW_SETTINGS_COMMAND_BAR_TITLE},
      {"uwCommandBarToggle", IDS_UW_SETTINGS_COMMAND_BAR_TOGGLE},
      {"uwCommandBarNote", IDS_UW_SETTINGS_COMMAND_BAR_NOTE},
      {"uwCommandBarCurrency", IDS_UW_SETTINGS_COMMAND_BAR_CURRENCY},
      {"uwCommandBarCurrencyDescription",
       IDS_UW_SETTINGS_COMMAND_BAR_CURRENCY_DESCRIPTION},
      {"uwCommandBarCurrencyRegion",
       IDS_UW_SETTINGS_COMMAND_BAR_CURRENCY_REGION},
      {"uwMouseTitle", IDS_UW_SETTINGS_MOUSE_TITLE},
      {"uwMouseOptionClick", IDS_UW_SETTINGS_MOUSE_OPTION_CLICK},
      {"uwMouseTaskRowPlus", IDS_UW_SETTINGS_MOUSE_TASK_ROW_PLUS},
      {"uwMouseDoubleClick", IDS_UW_SETTINGS_MOUSE_DOUBLE_CLICK},
      {"uwMouseMiddleClick", IDS_UW_SETTINGS_MOUSE_MIDDLE_CLICK},
      {"uwOrganizationPause1Hour", IDS_UW_PAUSE_1HOUR},
      {"uwOrganizationPauseUntilLaunch", IDS_UW_PAUSE_UNTIL_LAUNCH},
      {"uwOrganizationPauseIndefinite", IDS_UW_PAUSE_INDEFINITE},
      {"uwOrganizationResume", IDS_UW_PAUSE_RESUME},
  };
  data_source->AddLocalizedStrings(kLocalizedStrings);
}

void UwOrganizationHandler::GetState(GetStateCallback callback) {
  std::move(callback).Run(CurrentState());
}

void UwOrganizationHandler::SetEnabled(bool on, SetEnabledCallback callback) {
  profile_->GetPrefs()->SetBoolean(kOrganizationEnabledPref, on);
  std::move(callback).Run(CurrentState());
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

void UwOrganizationHandler::SetThresholds(mojom::ThresholdsPtr thresholds,
                                          SetThresholdsCallback callback) {
  SaveThresholds(profile_->GetPrefs(),
                 {thresholds->move, thresholds->new_task, thresholds->rename});
  std::move(callback).Run(CurrentThresholds());
}

mojom::StatePtr UwOrganizationHandler::CurrentState() const {
  const PrefService* prefs = profile_->GetPrefs();
  const DecisionModelConfig decision = ReadDecisionModelConfig(prefs);
  const AiConnectionConfig language = ReadAiConnectionConfig(prefs);
  auto state = mojom::State::New();
  state->enabled = prefs->GetBoolean(kOrganizationEnabledPref);
  state->waiting = WaitingFor(prefs);
  state->decision_model = IsComplete(decision) ? decision.model : "";
  state->language_model = IsComplete(language) ? language.model : "";
  state->pause_mode = static_cast<int32_t>(ReadPauseState(prefs));
  state->thresholds = CurrentThresholds();
  state->command_bar = prefs->GetBoolean(kCommandBarEnabledPref);
  state->currency = prefs->GetString(kCommandBarCurrencyPref);
  state->region_currency = RegionCurrency();
  state->currencies = Currencies();
  state->option_click_closes_branch =
      prefs->GetBoolean(kOptionClickClosesBranchPref);
  state->task_row_new_tab = prefs->GetBoolean(kTaskRowNewTabPref);
  state->double_click_opens_tab = prefs->GetBoolean(kDoubleClickOpensTabPref);
  state->middle_click_closes_task =
      prefs->GetBoolean(kMiddleClickClosesTaskPref);
  return state;
}

mojom::ThresholdsPtr UwOrganizationHandler::CurrentThresholds() const {
  const Thresholds needed = ReadThresholds(profile_->GetPrefs());
  return mojom::Thresholds::New(needed.confidence, needed.new_task,
                                needed.new_name);
}

void UwOrganizationHandler::OpenThemePicker() {
  if (BrowserWindowInterface* window =
          ProfileBrowserCollection::GetForProfile(profile_)
              ->GetLastActiveBrowser()) {
    chrome::ExecuteCommand(window, kShowThemePicker);
  }
}

void UwOrganizationHandler::SetCommandBar(bool on,
                                          SetCommandBarCallback callback) {
  profile_->GetPrefs()->SetBoolean(kCommandBarEnabledPref, on);
  std::move(callback).Run();
}

std::vector<std::string> UwOrganizationHandler::Currencies() const {
  std::vector<std::string> codes;
  if (mojom::ExchangeRatesPtr rates = ExchangeRates::For(profile_)->Saved()) {
    for (const auto& [code, rate] : rates->per_euro) {
      codes.push_back(code);
    }
    codes.push_back("EUR");
    std::ranges::sort(codes);
  }
  return codes;
}

void UwOrganizationHandler::SetCurrency(const std::string& code,
                                        SetCurrencyCallback callback) {
  if (code.empty() || std::ranges::contains(Currencies(), code)) {
    profile_->GetPrefs()->SetString(kCommandBarCurrencyPref, code);
  }
  std::move(callback).Run();
}

void UwOrganizationHandler::SetMouseAction(mojom::MouseAction action,
                                           bool on,
                                           SetMouseActionCallback callback) {
  profile_->GetPrefs()->SetBoolean(MouseActionPref(action), on);
  std::move(callback).Run();
}

}  // namespace uw
