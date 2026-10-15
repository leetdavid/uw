// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#ifndef UW_BROWSER_TAB_ORGANIZATION_AI_CONNECTION_H_
#define UW_BROWSER_TAB_ORGANIZATION_AI_CONNECTION_H_

#include <optional>
#include <string>
#include <string_view>

#include "base/time/time.h"
#include "url/gurl.h"
#include "uw/components/ai_providers/providers.h"
#include "uw/components/tab_organization/ai_protocol.h"
#include "uw/components/tab_organization/placement.h"

class PrefService;
namespace user_prefs {
class PrefRegistrySyncable;
}

namespace uw {

// Whether automatic organization runs while its models have passed their
// tests. The models themselves come from the providers on uw://providers.
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

// The connections saved before uw://providers. MoveConnectionsToProviders
// reads them once and clears them.
inline constexpr char kOrganizationModePref[] = "uw.organization.mode";
inline constexpr char kOrganizationEndpointPref[] = "uw.organization.endpoint";
inline constexpr char kOrganizationModelPref[] = "uw.organization.model";
inline constexpr char kOrganizationKeyAccountPref[] =
    "uw.organization.key_account";
inline constexpr char kDecisionProviderPref[] =
    "uw.organization.decision.provider";
inline constexpr char kDecisionEndpointPref[] =
    "uw.organization.decision.endpoint";
inline constexpr char kDecisionModelPref[] = "uw.organization.decision.model";
inline constexpr char kDecisionKeyAccountPref[] =
    "uw.organization.decision.key_account";
inline constexpr char kAgentModePref[] = "uw.agent.mode";
inline constexpr char kAgentEndpointPref[] = "uw.agent.endpoint";
inline constexpr char kAgentModelPref[] = "uw.agent.model";
inline constexpr char kAgentKeyAccountPref[] = "uw.agent.key_account";

// What uw://providers says once after the connections moved. Holds an
// UpgradeNotice.
inline constexpr char kUpgradeNoticePref[] = "uw.providers.upgrade_notice";

enum class UpgradeNotice {
  kNone = 0,
  // Each saved connection became a provider.
  kMoved = 1,
  // As kMoved, and connections with the same address and key became one.
  kMerged = 2,
  // A key could not be moved, so the old connections stay for the next
  // launch.
  kFailed = 3,
};

// How long uw waits for a provider's answer, while browsing and in a
// connection test alike. A local model can take this long to load, and a
// model that thinks first is slow to answer.
inline constexpr base::TimeDelta kProviderTimeout = base::Seconds(30);

// A language or agent model as a request needs it.
struct AiConnectionConfig {
  std::string endpoint;
  std::string model;
  // Keychain account id holding the API key; empty when there is none.
  std::string key_account;
};

// The decision model as a request needs it.
struct DecisionModelConfig {
  DecisionFormat format = DecisionFormat::kDecisions;
  std::string endpoint;
  std::string model;
  std::string key_account;
};

// Global suspension of automatic organization per the product pause choices.
// "Until next launch" is process state: it resets when uw next starts.
enum class PauseMode {
  kOff = 0,
  kTimed = 1,
  kUntilLaunch = 2,
  kIndefinite = 3
};

void RegisterAiConnectionProfilePrefs(
    user_prefs::PrefRegistrySyncable* registry);

// Turns the connections saved before uw://providers into providers, once.
// Connections with the same address and key become one provider. The old
// preferences and Keychain accounts go once every key has moved; until then
// they stay, and the next launch tries again.
void MoveConnectionsToProviders(PrefService* prefs);

// The models picked on uw://providers. Empty when the slot holds no model, or
// its provider is gone or cannot serve it.
AiConnectionConfig ReadAiConnectionConfig(const PrefService* prefs);
// The language model's connection until an agent model is picked.
AiConnectionConfig ReadAgentModelConfig(const PrefService* prefs);
DecisionModelConfig ReadDecisionModelConfig(const PrefService* prefs);

// Accepts https URLs, plus http loopback for local testing. Rejects
// credentials, fragments, and invalid URLs.
bool IsValidProviderEndpoint(std::string_view endpoint);

// Request URLs under a base endpoint like https://api.openai.com/v1.
GURL ChatCompletionsUrl(std::string_view endpoint);
GURL ModelsUrl(std::string_view endpoint);
GURL DecisionUrl(const DecisionModelConfig& config);

// True when the connection has everything a request needs.
bool IsComplete(const AiConnectionConfig& config);
bool IsComplete(const DecisionModelConfig& config);

// The language or decision model that is picked but has not passed its test
// since. Automatic organization waits for it.
std::optional<ModelSlot> UntestedModel(const PrefService* prefs);

// True when automatic organization may send requests: the user turned it on,
// at least one of its models is complete, and every picked one passed its
// test.
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
