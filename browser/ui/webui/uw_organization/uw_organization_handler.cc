// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/browser/ui/webui/uw_organization/uw_organization_handler.h"

#include <algorithm>
#include <optional>
#include <string>
#include <utility>

#include "base/functional/bind.h"
#include "base/task/thread_pool.h"
#include "chrome/browser/browser_process.h"  // nogncheck
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_window/public/profile_browser_collection.h"
#include "chrome/grit/generated_resources.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/web_ui_data_source.h"
#include "ui/base/webui/web_ui_util.h"
#include "uw/browser/ai_providers/models.h"
#include "uw/browser/tab_organization/ai_connection.h"
#include "uw/browser/branding/report_issue.h"
#include "uw/browser/problem_reports/problem_reports.h"
#include "uw/browser/tab_organization/commands.h"
#include "uw/browser/ui/glass/glass.h"
#include "uw/browser/ui/glass/glass_prefs.h"
#include "uw/browser/ui/views/mini_player/mini_player_prefs.h"
#include "uw/browser/ui/webui/uw_command_bar/exchange_rates.h"
#include "uw/browser/uw_command/command_channel.h"
#include "uw/browser/uw_command/command_install.h"
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
    case mojom::MouseAction::kDragSidewaysNestsTabs:
      return kDragSidewaysNestsTabsPref;
  }
}

const char* MiniPlayerPref(mojom::MiniPlayerSwitch which) {
  switch (which) {
    case mojom::MiniPlayerSwitch::kOnTabSwitch:
      return kMiniPlayerOnTabSwitchPref;
    case mojom::MiniPlayerSwitch::kFloatInBackground:
      return kMiniPlayerFloatInBackgroundPref;
  }
}

mojom::Waiting WaitingFor(const PrefService* prefs) {
  return IsComplete(ReadDecisionModelConfig(prefs)) ||
                 IsComplete(ReadQuickLanguageConfig(prefs))
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
      {"uwGlassBehind", IDS_UW_SETTINGS_GLASS_BEHIND},
      {"uwGlassBehindDescription", IDS_UW_SETTINGS_GLASS_BEHIND_DESCRIPTION},
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
      {"uwMouseDragSideways", IDS_UW_SETTINGS_MOUSE_DRAG_SIDEWAYS},
      {"uwMiniPlayerTitle", IDS_UW_SETTINGS_MINI_PLAYER_TITLE},
      {"uwMiniPlayerMode", IDS_UW_SETTINGS_MINI_PLAYER_MODE},
      {"uwMiniPlayerDock", IDS_UW_SETTINGS_MINI_PLAYER_DOCK},
      {"uwMiniPlayerDockDescription",
       IDS_UW_SETTINGS_MINI_PLAYER_DOCK_DESCRIPTION},
      {"uwMiniPlayerDontDock", IDS_UW_SETTINGS_MINI_PLAYER_DONT_DOCK},
      {"uwMiniPlayerDontDockDescription",
       IDS_UW_SETTINGS_MINI_PLAYER_DONT_DOCK_DESCRIPTION},
      {"uwMiniPlayerOff", IDS_UW_SETTINGS_MINI_PLAYER_OFF},
      {"uwMiniPlayerOffDescription",
       IDS_UW_SETTINGS_MINI_PLAYER_OFF_DESCRIPTION},
      {"uwMiniPlayerLive", IDS_UW_SETTINGS_MINI_PLAYER_LIVE},
      {"uwMiniPlayerLiveDescription",
       IDS_UW_SETTINGS_MINI_PLAYER_LIVE_DESCRIPTION},
      {"uwMiniPlayerOnTabSwitch", IDS_UW_SETTINGS_MINI_PLAYER_ON_TAB_SWITCH},
      {"uwMiniPlayerOnTabSwitchDescription",
       IDS_UW_SETTINGS_MINI_PLAYER_ON_TAB_SWITCH_DESCRIPTION},
      {"uwMiniPlayerFloat", IDS_UW_SETTINGS_MINI_PLAYER_FLOAT},
      {"uwMiniPlayerFloatDescription",
       IDS_UW_SETTINGS_MINI_PLAYER_FLOAT_DESCRIPTION},
      {"uwOrganizationPause1Hour", IDS_UW_PAUSE_1HOUR},
      {"uwOrganizationPauseUntilLaunch", IDS_UW_PAUSE_UNTIL_LAUNCH},
      {"uwOrganizationPauseIndefinite", IDS_UW_PAUSE_INDEFINITE},
      {"uwOrganizationResume", IDS_UW_PAUSE_RESUME},
      {"uwCommandTitle", IDS_UW_SETTINGS_COMMAND_TITLE},
      {"uwProblemReportsTitle", IDS_UW_SETTINGS_PROBLEM_REPORTS_TITLE},
      {"uwProblemReportsSend", IDS_UW_SETTINGS_PROBLEM_REPORTS_SEND},
      {"uwProblemReportsSendNote", IDS_UW_SETTINGS_PROBLEM_REPORTS_SEND_NOTE},
      {"uwProblemReportsCrashes", IDS_UW_SETTINGS_PROBLEM_REPORTS_CRASHES},
      {"uwProblemReportsPages", IDS_UW_SETTINGS_PROBLEM_REPORTS_PAGES},
      {"uwProblemReportsWebsites", IDS_UW_SETTINGS_PROBLEM_REPORTS_WEBSITES},
      {"uwReportIssue", IDS_UW_SETTINGS_REPORT_ISSUE},
      {"uwReportIssueNote", IDS_UW_SETTINGS_REPORT_ISSUE_NOTE},
      {"uwCommandRow", IDS_UW_SETTINGS_COMMAND_ROW},
      {"uwCommandDescription", IDS_UW_SETTINGS_COMMAND_DESCRIPTION},
      {"uwCommandNote", IDS_UW_SETTINGS_COMMAND_NOTE},
      {"uwCommandInstall", IDS_UW_SETTINGS_COMMAND_INSTALL},
      {"uwCommandUninstall", IDS_UW_SETTINGS_COMMAND_UNINSTALL},
      {"uwCommandInstalledAt", IDS_UW_SETTINGS_COMMAND_INSTALLED_AT},
      {"uwCommandTry", IDS_UW_SETTINGS_COMMAND_TRY},
      {"uwCommandCancelled", IDS_UW_SETTINGS_COMMAND_CANCELLED},
      {"uwCommandFailed", IDS_UW_SETTINGS_COMMAND_FAILED},
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
  state->can_show_glass = CanShowGlass();
  state->glass_behind =
      g_browser_process->local_state()->GetBoolean(kGlassBehindPref);
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
  state->drag_sideways_nests_tabs =
      prefs->GetBoolean(kDragSidewaysNestsTabsPref);
  const PrefService* local_state = g_browser_process->local_state();
  state->mini_player_mode =
      static_cast<mojom::MiniPlayerMode>(MiniPlayerModeOf(local_state));
  state->mini_player_on_tab_switch =
      local_state->GetBoolean(kMiniPlayerOnTabSwitchPref);
  state->mini_player_float_in_background =
      local_state->GetBoolean(kMiniPlayerFloatInBackgroundPref);
  for (const SiteWindowSite& site : SiteWindowSites(local_state)) {
    state->live_sites.push_back(mojom::LiveSite::New(site.site, site.live));
  }
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

void UwOrganizationHandler::SetGlassBehind(bool on,
                                           SetGlassBehindCallback callback) {
  g_browser_process->local_state()->SetBoolean(kGlassBehindPref, on);
  std::move(callback).Run();
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

void UwOrganizationHandler::SetMiniPlayerMode(
    mojom::MiniPlayerMode mode,
    SetMiniPlayerModeCallback callback) {
  g_browser_process->local_state()->SetInteger(kMiniPlayerModePref,
                                               static_cast<int>(mode));
  std::move(callback).Run();
}

void UwOrganizationHandler::SetSiteLive(const std::string& site,
                                        bool live,
                                        SetSiteLiveCallback callback) {
  uw::SetSiteLive(g_browser_process->local_state(), site, live);
  std::move(callback).Run();
}

void UwOrganizationHandler::SetMiniPlayer(mojom::MiniPlayerSwitch which,
                                          bool on,
                                          SetMiniPlayerCallback callback) {
  g_browser_process->local_state()->SetBoolean(MiniPlayerPref(which), on);
  std::move(callback).Run();
}

namespace {

mojom::CommandStatePtr CommandStateOf(mojom::CommandProblem problem,
                                      bool installed) {
  return mojom::CommandState::New(installed, CommandLink().value(), problem);
}

mojom::CommandProblem ProblemOf(CommandInstall result) {
  switch (result) {
    case CommandInstall::kDone:
      return mojom::CommandProblem::kNone;
    case CommandInstall::kCancelled:
      return mojom::CommandProblem::kCancelled;
    case CommandInstall::kFailed:
      return mojom::CommandProblem::kFailed;
  }
}

// Links or unlinks the command off the UI thread, since macOS's password
// prompt waits for the user, then opens or closes the channel to match.
void ChangeCommand(base::OnceCallback<CommandInstall()> change,
                   base::OnceCallback<void(mojom::CommandStatePtr)> done) {
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::MayBlock()},
      base::BindOnce(
          [](base::OnceCallback<CommandInstall()> change) {
            const CommandInstall result = std::move(change).Run();
            return std::make_pair(result, CommandInstalled());
          },
          std::move(change)),
      base::BindOnce(
          [](base::OnceCallback<void(mojom::CommandStatePtr)> done,
             std::pair<CommandInstall, bool> outcome) {
            if (outcome.second) {
              CommandChannel::Get().Start();
            } else {
              CommandChannel::Get().Close();
            }
            std::move(done).Run(
                CommandStateOf(ProblemOf(outcome.first), outcome.second));
          },
          std::move(done)));
}

}  // namespace

void UwOrganizationHandler::GetCommand(GetCommandCallback callback) {
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::MayBlock()}, base::BindOnce(&CommandInstalled),
      base::BindOnce(&CommandStateOf, mojom::CommandProblem::kNone)
          .Then(std::move(callback)));
}

void UwOrganizationHandler::GetProblemReports(
    GetProblemReportsCallback callback) {
  const ProblemReports& reports = ProblemReports::Get();
  std::move(callback).Run(mojom::ProblemReportsState::New(
      reports.available(), reports.sending(), NewIssueUrl().spec()));
}

void UwOrganizationHandler::SetProblemReports(
    bool send,
    SetProblemReportsCallback callback) {
  ProblemReports::Get().Answer(send);
  std::move(callback).Run();
}

void UwOrganizationHandler::InstallCommand(InstallCommandCallback callback) {
  ChangeCommand(base::BindOnce(&LinkCommand, CommandLink(), CommandBinary()),
                std::move(callback));
}

void UwOrganizationHandler::UninstallCommand(
    UninstallCommandCallback callback) {
  ChangeCommand(base::BindOnce(&UnlinkCommand, CommandLink()),
                std::move(callback));
}

}  // namespace uw
