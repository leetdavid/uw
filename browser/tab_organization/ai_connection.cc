// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/browser/tab_organization/ai_connection.h"

#include "components/pref_registry/pref_registry_syncable.h"
#include "components/prefs/pref_service.h"
#include "uw/browser/ai_providers/models.h"

namespace uw {
namespace {

// "Pause until next launch" must reset when uw next starts, so it lives in
// process state rather than in a profile preference.
bool g_paused_until_launch = false;

}  // namespace

void RegisterAiConnectionProfilePrefs(
    user_prefs::PrefRegistrySyncable* registry) {
  registry->RegisterBooleanPref(kOrganizationEnabledPref, false);
  registry->RegisterBooleanPref(kOrganizationShowActivityPref, false);
  registry->RegisterDoublePref(kOrganizationMoveThresholdPref,
                               kDefaultThresholds.confidence);
  registry->RegisterDoublePref(kOrganizationNewTaskThresholdPref,
                               kDefaultThresholds.new_task);
  registry->RegisterDoublePref(kOrganizationRenameThresholdPref,
                               kDefaultThresholds.new_name);
  registry->RegisterListPref(kAgentDisabledSkillsPref);
  registry->RegisterStringPref(kAgentApprovalModePref, "auto");
  registry->RegisterBooleanPref(kCommandBarEnabledPref, true);
  registry->RegisterIntegerPref(kOrganizationPauseModePref,
                                static_cast<int>(PauseMode::kOff));
  registry->RegisterDoublePref(kOrganizationPauseUntilPref, 0);
  RegisterModelPrefs(registry);
}

bool OrganizationReady(const PrefService* prefs) {
  return prefs && prefs->GetBoolean(kOrganizationEnabledPref) &&
         (IsComplete(ReadDecisionModelConfig(prefs)) ||
          IsComplete(ReadAiConnectionConfig(prefs)));
}

Thresholds ReadThresholds(const PrefService* prefs) {
  return {AsThreshold(prefs->GetDouble(kOrganizationMoveThresholdPref)),
          AsThreshold(prefs->GetDouble(kOrganizationNewTaskThresholdPref)),
          AsThreshold(prefs->GetDouble(kOrganizationRenameThresholdPref))};
}

void SaveThresholds(PrefService* prefs, const Thresholds& thresholds) {
  prefs->SetDouble(kOrganizationMoveThresholdPref,
                   AsThreshold(thresholds.confidence));
  prefs->SetDouble(kOrganizationNewTaskThresholdPref,
                   AsThreshold(thresholds.new_task));
  prefs->SetDouble(kOrganizationRenameThresholdPref,
                   AsThreshold(thresholds.new_name));
}

PauseMode ReadPauseState(const PrefService* prefs) {
  if (g_paused_until_launch) {
    return PauseMode::kUntilLaunch;
  }
  if (!prefs) {
    return PauseMode::kOff;
  }
  switch (prefs->GetInteger(kOrganizationPauseModePref)) {
    case static_cast<int>(PauseMode::kTimed):
      return base::Time::Now() < PausedUntil(prefs) ? PauseMode::kTimed
                                                    : PauseMode::kOff;
    case static_cast<int>(PauseMode::kIndefinite):
      return PauseMode::kIndefinite;
    default:
      return PauseMode::kOff;
  }
}

base::Time PausedUntil(const PrefService* prefs) {
  return base::Time::FromDeltaSinceWindowsEpoch(base::Microseconds(
      static_cast<int64_t>(prefs->GetDouble(kOrganizationPauseUntilPref))));
}

void SetPauseState(PrefService* prefs,
                   PauseMode mode,
                   base::TimeDelta duration) {
  g_paused_until_launch = mode == PauseMode::kUntilLaunch;
  if (!prefs) {
    return;
  }
  prefs->SetInteger(kOrganizationPauseModePref, static_cast<int>(mode));
  // The timed-pause deadline is stored in microseconds since the Windows
  // epoch.
  prefs->SetDouble(kOrganizationPauseUntilPref,
                   mode == PauseMode::kTimed ? (base::Time::Now() + duration)
                                                   .ToDeltaSinceWindowsEpoch()
                                                   .InMicroseconds()
                                             : 0);
}

}  // namespace uw
