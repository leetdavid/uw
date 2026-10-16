// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

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

// Whether Cmd+T opens the command bar instead of a blank tab.
inline constexpr char kCommandBarEnabledPref[] = "uw.command_bar.enabled";

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
// `duration` applies to PauseMode::kTimed.
void SetPauseState(PrefService* prefs,
                   PauseMode mode,
                   base::TimeDelta duration = base::TimeDelta());

}  // namespace uw

#endif  // UW_BROWSER_TAB_ORGANIZATION_AI_CONNECTION_H_
