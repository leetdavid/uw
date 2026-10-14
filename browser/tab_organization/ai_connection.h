// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#ifndef UW_BROWSER_TAB_ORGANIZATION_AI_CONNECTION_H_
#define UW_BROWSER_TAB_ORGANIZATION_AI_CONNECTION_H_

#include <string>
#include <string_view>

#include "base/time/time.h"
#include "url/gurl.h"
#include "uw/components/tab_organization/ai_protocol.h"
#include "uw/components/tab_organization/placement.h"

class PrefService;
namespace user_prefs {
class PrefRegistrySyncable;
}

namespace uw {

// Profile preferences for the classification connection behind automatic
// organization. Secrets live in macOS Keychain under non-secret account ids;
// only the mode, endpoint, model id, and enablement live here.
inline constexpr char kOrganizationModePref[] = "uw.organization.mode";
inline constexpr char kOrganizationEndpointPref[] = "uw.organization.endpoint";
inline constexpr char kOrganizationModelPref[] = "uw.organization.model";
inline constexpr char kOrganizationKeyAccountPref[] =
    "uw.organization.key_account";
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

// The optional decision model. It decides where a tab branch goes; the
// language model above names new tasks.
inline constexpr char kDecisionProviderPref[] =
    "uw.organization.decision.provider";
inline constexpr char kDecisionEndpointPref[] =
    "uw.organization.decision.endpoint";
inline constexpr char kDecisionModelPref[] = "uw.organization.decision.model";
inline constexpr char kDecisionKeyAccountPref[] =
    "uw.organization.decision.key_account";

// The model behind chats. Until the user saves one, chats use the language
// model's connection.
inline constexpr char kAgentModePref[] = "uw.agent.mode";
inline constexpr char kAgentEndpointPref[] = "uw.agent.endpoint";
inline constexpr char kAgentModelPref[] = "uw.agent.model";
inline constexpr char kAgentKeyAccountPref[] = "uw.agent.key_account";

// Keychain accounts holding the API keys.
inline constexpr char kLanguageKeyAccount[] = "organization";
inline constexpr char kDecisionKeyAccount[] = "decision";
inline constexpr char kAgentKeyAccount[] = "agent";

inline constexpr char kJevDefaultEndpoint[] = "https://api.typesafe.ai/v1";
inline constexpr char kJevDefaultModel[] = "jev-latest";
inline constexpr char kOpenAiDefaultEndpoint[] = "https://api.openai.com/v1";
inline constexpr char kOpenAiDecisionsDefaultModel[] = "gpt-6-luna";

// How long uw waits for a provider's answer, while browsing and in a
// connection test alike. A local model can take this long to load, and a
// model that thinks first is slow to answer.
inline constexpr base::TimeDelta kProviderTimeout = base::Seconds(30);

// Default Ollama base endpoint. Ollama needs no API key.
inline constexpr char kOllamaDefaultEndpoint[] = "http://localhost:11434/v1";

// The two connection shapes behind automatic organization. Both speak the
// OpenAI-compatible chat-completions protocol.
enum class ConnectionMode { kOllama = 0, kOpenAiCompatible = 1 };

struct AiConnectionConfig {
  ConnectionMode mode = ConnectionMode::kOllama;
  std::string endpoint;
  std::string model;
  // Keychain account id holding the API key; empty in Ollama mode.
  std::string key_account;
};

// Jev speaks TypeSafe System One. The other two speak the OpenAI Decisions
// API, the second at a base URL the user enters.
enum class DecisionProvider {
  kNone = 0,
  kJev = 1,
  kOpenAi = 2,
  kOpenAiCompatible = 3,
};

struct DecisionModelConfig {
  DecisionProvider provider = DecisionProvider::kNone;
  std::string endpoint;
  std::string model;
  // Keychain account id holding the API key.
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
AiConnectionConfig ReadAiConnectionConfig(const PrefService* prefs);
// The language model's connection until an agent model is saved.
AiConnectionConfig ReadAgentModelConfig(const PrefService* prefs);
// Fills in the provider's default endpoint and model where none is saved.
DecisionModelConfig ReadDecisionModelConfig(const PrefService* prefs);

// Accepts https URLs, plus http loopback for local testing. Rejects
// credentials, fragments, and invalid URLs.
bool IsValidProviderEndpoint(std::string_view endpoint);

// Request URLs under a base endpoint like https://api.openai.com/v1.
GURL ChatCompletionsUrl(std::string_view endpoint);
GURL ModelsUrl(std::string_view endpoint);
DecisionFormat FormatFor(DecisionProvider provider);
GURL DecisionUrl(const DecisionModelConfig& config);

// True when the connection has everything a request needs.
bool IsComplete(const AiConnectionConfig& config);
bool IsComplete(const DecisionModelConfig& config);

// True when automatic organization may send requests: it was enabled by a
// successful validation and at least one connection is complete.
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
