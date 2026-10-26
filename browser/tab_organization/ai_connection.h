// Copyright 2026 The uw Authors. All rights reserved.

#ifndef UW_BROWSER_TAB_ORGANIZATION_AI_CONNECTION_H_
#define UW_BROWSER_TAB_ORGANIZATION_AI_CONNECTION_H_

#include "base/time/time.h"
#include "uw/components/tab_organization/placement.h"

class PrefService;
namespace user_prefs {
class PrefRegistrySyncable;
}

namespace uw {

// Whether automatic organization runs while its models have passed their
// tests. The models themselves come from the providers on uw://providers;
// see uw/browser/ai_providers/models.h.
inline constexpr char kOrganizationEnabledPref[] = "uw.organization.enabled";
inline constexpr char kOrganizationPauseModePref[] =
    "uw.organization.pause_mode";
inline constexpr char kOrganizationPauseUntilPref[] =
    "uw.organization.pause_until";
// How sure a model must be before uw moves a branch, starts a task, or
// renames one. See Thresholds.
inline constexpr char kOrganizationMoveThresholdPref[] =
    "uw.organization.threshold.move";
inline constexpr char kOrganizationNewTaskThresholdPref[] =
    "uw.organization.threshold.new_task";
inline constexpr char kOrganizationRenameThresholdPref[] =
    "uw.organization.threshold.rename";
// Whether the sidebar says what each automatic organization request did.
inline constexpr char kOrganizationShowActivityPref[] =
    "uw.organization.show_activity";

// The names of the skills the user turned off. Every other skill is on.
inline constexpr char kAgentDisabledSkillsPref[] = "uw.agent.disabled_skills";

// The approval mode new chats start on: "ask_first", "auto", or "full_auto".
inline constexpr char kAgentApprovalModePref[] = "uw.agent.approval_mode";

// Whether chats recall and keep agent memory. Off pauses it and keeps every
// memory. See docs/plans/agent-memory.md.
inline constexpr char kAgentMemoryPref[] = "uw.agent.memory";

// Whether Cmd+T opens the command bar instead of a blank tab.
inline constexpr char kCommandBarEnabledPref[] = "uw.command_bar.enabled";

// Whether a launch after a crash restores the last session without Chromium's
// "Restore pages?" prompt. The prompt's checkbox turns it on.
inline constexpr char kRestoreAfterCrashPref[] =
    "uw.session.restore_after_crash";

// The switches of the tab list's mouse actions. Each is on by default.
inline constexpr char kOptionClickClosesBranchPref[] =
    "uw.mouse.option_click_closes_branch";
inline constexpr char kTaskRowNewTabPref[] = "uw.mouse.task_row_new_tab";
inline constexpr char kDoubleClickOpensTabPref[] =
    "uw.mouse.double_click_opens_tab";
inline constexpr char kMiddleClickClosesTaskPref[] =
    "uw.mouse.middle_click_closes_task";

// The European Central Bank's rates the command bar last downloaded: "date",
// "fetched" in seconds since the epoch, and "per_euro".
inline constexpr char kCommandBarRatesPref[] = "uw.command_bar.rates";

// The currency a bare amount converts to. Empty for the Mac's region's.
inline constexpr char kCommandBarCurrencyPref[] = "uw.command_bar.currency";

// Global suspension of automatic organization per the product pause choices.
// "Until next launch" is process state: it resets when uw next starts.
enum class PauseMode {
  kOff = 0,
  kTimed = 1,
  kUntilLaunch = 2,
  kIndefinite = 3
};

// Registers these preferences and the models'.
void RegisterAiConnectionProfilePrefs(
    user_prefs::PrefRegistrySyncable* registry);

// True when automatic organization may send requests: the user turned it on
// and at least one of its models is complete. A model need not pass a test
// first.
bool OrganizationReady(const PrefService* prefs);

// The user's thresholds. A stored value outside the range counts as the
// nearest one in it.
Thresholds ReadThresholds(const PrefService* prefs);
void SaveThresholds(PrefService* prefs, const Thresholds& thresholds);

PauseMode ReadPauseState(const PrefService* prefs);
// When a pause of PauseMode::kTimed ends.
base::Time PausedUntil(const PrefService* prefs);
// `duration` applies to PauseMode::kTimed.
void SetPauseState(PrefService* prefs,
                   PauseMode mode,
                   base::TimeDelta duration = base::TimeDelta());

}  // namespace uw

#endif  // UW_BROWSER_TAB_ORGANIZATION_AI_CONNECTION_H_
