// Copyright 2026 The uw Authors. All rights reserved.

#ifndef UW_BROWSER_AI_PROVIDERS_DECISION_PROBES_H_
#define UW_BROWSER_AI_PROVIDERS_DECISION_PROBES_H_

#include <deque>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/supports_user_data.h"
#include "base/time/time.h"
#include "uw/browser/ai_providers/remote_model_client.h"
#include "uw/components/ai_providers/providers.h"

class PrefRegistrySimple;
class Profile;

namespace uw {

// What DecisionProbes keeps in the profile: {provider id: {"address": ...,
// "models": {model: {"decides": bool, "at": time}}}}.
inline constexpr char kDecisionProbesPref[] = "uw.providers.decision_probes";

// Whether a model makes decisions. kUnknown while uw asks it, or when asking
// failed.
enum class Decides { kNo, kYes, kUnknown };

// Where a provider's own model list does not say which models make
// decisions, as at OpenAI, uw sends each model one small decision request and
// keeps the answer in the profile, per ADR 0025. Answers belong to the
// provider's address, so moving it starts over.
class DecisionProbes : public base::SupportsUserData::Data {
 public:
  struct Answers {
    std::map<std::string, Decides> models;
    // True while uw waits for an answer about one of the provider's models.
    bool asking = false;
  };

  // Made the first time a provider is listed.
  static DecisionProbes* For(Profile* profile);
  static void RegisterPrefs(PrefRegistrySimple* registry);
  // Whether uw asks `provider`'s models: the OpenAI preset and a Custom
  // provider in a decision format, once saved.
  static bool Asks(const Provider& provider);

  DecisionProbes(const DecisionProbes&) = delete;
  DecisionProbes& operator=(const DecisionProbes&) = delete;
  ~DecisionProbes() override;

  // What uw knows about each of `models`, the provider's whole list. Asks the
  // models it has no answer about, or whose no is a week old, unless one
  // failed in the last few minutes. Forgets models the list no longer has.
  Answers Ask(const Provider& provider,
              const std::string& key,
              const std::vector<std::string>& models);

  // Runs `done` once the next answer about the provider comes, or at once
  // when uw is asking it nothing.
  void WaitForAnswer(const std::string& provider_id, base::OnceClosure done);

  // Drops the answers about a provider and its requests still out, as when
  // its key changes or it is removed.
  void Forget(const std::string& provider_id);

 private:
  // The requests out to one provider, and its key while they last.
  struct Asking;

  explicit DecisionProbes(Profile* profile);

  void SendNext(const std::string& provider_id);
  void OnAnswered(std::string provider_id,
                  std::string model,
                  RemoteModelClient::Outcome outcome);
  void Keep(const Provider& provider, const std::string& model, bool decides);

  const raw_ptr<Profile> profile_;
  std::map<std::string, std::unique_ptr<Asking>> asking_;
  // When a request about a model failed without an answer, by provider and
  // model.
  std::map<std::pair<std::string, std::string>, base::TimeTicks> failed_;
  std::map<std::string, std::vector<base::OnceClosure>> waiting_;
  base::WeakPtrFactory<DecisionProbes> weak_factory_{this};
};

}  // namespace uw

#endif  // UW_BROWSER_AI_PROVIDERS_DECISION_PROBES_H_
