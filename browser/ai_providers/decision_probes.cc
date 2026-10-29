// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/browser/ai_providers/decision_probes.h"

#include <algorithm>
#include <optional>
#include <utility>

#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/json/values_util.h"
#include "base/memory/ptr_util.h"
#include "base/values.h"
#include "chrome/browser/profiles/profile.h"
#include "components/prefs/pref_registry_simple.h"
#include "components/prefs/pref_service.h"
#include "components/prefs/scoped_user_pref_update.h"
#include "content/public/browser/storage_partition.h"
#include "net/http/http_status_code.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"
#include "uw/browser/ai_providers/models.h"
#include "uw/components/agent/check_questions.h"

namespace uw {
namespace {

constexpr char kUserDataKey[] = "uw_decision_probes";
constexpr char kAddress[] = "address";
constexpr char kModels[] = "models";
constexpr char kDecides[] = "decides";
constexpr char kAt[] = "at";

// A provider can start to take a model it turned down.
constexpr base::TimeDelta kAskNoAgain = base::Days(7);
// A request that got no answer, such as with no network, waits this long
// before uw tries it again, so listing the models again soon after does not
// repeat it.
constexpr base::TimeDelta kRetryFailed = base::Minutes(5);
constexpr size_t kAtOnce = 4;

constexpr char kInput[] = "uw checks that this model can answer.";
constexpr char kQuestion[] = "answer";

DecisionFormat FormatFor(const Provider& provider) {
  return DecisionFormatOf(provider) == ProviderFormat::kSystemOne
             ? DecisionFormat::kSystemOne
             : DecisionFormat::kDecisions;
}

// The answers kept about `provider` at its present address, or null.
const base::DictValue* KeptModels(const PrefService& prefs,
                                  const Provider& provider) {
  const base::DictValue* entry =
      prefs.GetDict(kDecisionProbesPref).FindDict(provider.id);
  const std::string* address = entry ? entry->FindString(kAddress) : nullptr;
  return address && *address == provider.address ? entry->FindDict(kModels)
                                                 : nullptr;
}

Decides KeptAnswer(const base::DictValue* models, const std::string& model) {
  const base::DictValue* kept = models ? models->FindDict(model) : nullptr;
  const std::optional<bool> decides =
      kept ? kept->FindBool(kDecides) : std::nullopt;
  const std::optional<base::Time> at =
      kept ? base::ValueToTime(kept->Find(kAt)) : std::nullopt;
  if (!decides || !at) {
    return Decides::kUnknown;
  }
  if (*decides) {
    return Decides::kYes;
  }
  return base::Time::Now() - *at < kAskNoAgain ? Decides::kNo
                                               : Decides::kUnknown;
}

}  // namespace

struct DecisionProbes::Asking {
  Provider provider;
  std::string key;
  std::deque<std::string> next;
  std::map<std::string, std::unique_ptr<RemoteModelClient>> out;
};

// static
DecisionProbes* DecisionProbes::For(Profile* profile) {
  auto* probes =
      static_cast<DecisionProbes*>(profile->GetUserData(kUserDataKey));
  if (!probes) {
    std::unique_ptr<DecisionProbes> made =
        base::WrapUnique(new DecisionProbes(profile));
    probes = made.get();
    profile->SetUserData(kUserDataKey, std::move(made));
  }
  return probes;
}

// static
void DecisionProbes::RegisterPrefs(PrefRegistrySimple* registry) {
  registry->RegisterDictionaryPref(kDecisionProbesPref);
}

// static
bool DecisionProbes::Asks(const Provider& provider) {
  if (provider.id.empty()) {
    return false;
  }
  return provider.preset == ProviderPreset::kOpenAi ||
         (provider.preset == ProviderPreset::kCustom &&
          DecisionFormatOf(provider).has_value());
}

// static
std::optional<bool> DecisionProbes::Decided(const PrefService& prefs,
                                            const Provider& provider,
                                            const std::string& model) {
  const base::DictValue* models = KeptModels(prefs, provider);
  const base::DictValue* kept = models ? models->FindDict(model) : nullptr;
  return kept ? kept->FindBool(kDecides) : std::nullopt;
}

DecisionProbes::DecisionProbes(Profile* profile) : profile_(profile) {}

DecisionProbes::~DecisionProbes() = default;

DecisionProbes::Answers DecisionProbes::Ask(
    const Provider& provider,
    const std::string& key,
    const std::vector<std::string>& models) {
  PrefService& prefs = *profile_->GetPrefs();
  const base::DictValue* kept = KeptModels(prefs, provider);
  bool stale =
      !kept && prefs.GetDict(kDecisionProbesPref).contains(provider.id);
  base::DictValue listed;
  if (kept) {
    for (const auto [model, answer] : *kept) {
      if (std::ranges::contains(models, model)) {
        listed.Set(model, answer.Clone());
      } else {
        stale = true;
      }
    }
  }
  if (stale) {
    ScopedDictPrefUpdate(&prefs, kDecisionProbesPref)
        ->Set(provider.id, base::DictValue()
                               .Set(kAddress, provider.address)
                               .Set(kModels, std::move(listed)));
    kept = KeptModels(prefs, provider);
  }

  Answers answers;
  std::unique_ptr<Asking>& asking = asking_[provider.id];
  for (const std::string& model : models) {
    const Decides decides = KeptAnswer(kept, model);
    answers.models[model] = decides;
    const auto failed = failed_.find({provider.id, model});
    if (decides != Decides::kUnknown ||
        (failed != failed_.end() &&
         base::TimeTicks::Now() - failed->second < kRetryFailed)) {
      continue;
    }
    if (!asking) {
      asking = std::make_unique<Asking>();
    }
    if (!asking->out.contains(model) &&
        !std::ranges::contains(asking->next, model)) {
      asking->next.push_back(model);
    }
  }
  if (!asking) {
    asking_.erase(provider.id);
    return answers;
  }
  asking->provider = provider;
  asking->key = key;
  SendNext(provider.id);
  answers.asking = asking_.contains(provider.id);
  return answers;
}

void DecisionProbes::WaitForAnswer(const std::string& provider_id,
                                   base::OnceClosure done) {
  if (!asking_.contains(provider_id)) {
    std::move(done).Run();
    return;
  }
  waiting_[provider_id].push_back(std::move(done));
}

void DecisionProbes::Forget(const std::string& provider_id) {
  ScopedDictPrefUpdate(profile_->GetPrefs(), kDecisionProbesPref)
      ->Remove(provider_id);
  asking_.erase(provider_id);
  std::erase_if(failed_, [&](const auto& failed) {
    return failed.first.first == provider_id;
  });
  for (base::OnceClosure& done : std::exchange(waiting_[provider_id], {})) {
    std::move(done).Run();
  }
  waiting_.erase(provider_id);
}

void DecisionProbes::SendNext(const std::string& provider_id) {
  const auto found = asking_.find(provider_id);
  if (found == asking_.end()) {
    return;
  }
  Asking& asking = *found->second;
  const DecisionFormat format = FormatFor(asking.provider);
  while (asking.out.size() < kAtOnce && !asking.next.empty()) {
    std::string model = std::move(asking.next.front());
    asking.next.pop_front();
    DecisionModelConfig config;
    config.format = format;
    config.endpoint = asking.provider.address;
    config.model = model;
    auto client = std::make_unique<RemoteModelClient>(
        profile_->GetDefaultStoragePartition()
            ->GetURLLoaderFactoryForBrowserProcess(),
        DecisionUrl(config), asking.key,
        BuildCheckQuestionsBody(format, model, kInput,
                                {{kQuestion,
                                  "Is this sentence in English?",
                                  {{"yes", "It is in English."},
                                   {"no", "It is in another language."}}}})
            .value_or(std::string()),
        kProviderTimeout,
        RemoteModelClient::KeyHeaderFor(asking.provider.preset,
                                        asking.provider.format));
    RemoteModelClient* sender = client.get();
    asking.out[model] = std::move(client);
    sender->Send(base::BindOnce(&DecisionProbes::OnAnswered,
                                weak_factory_.GetWeakPtr(), provider_id,
                                model));
  }
  if (asking.out.empty()) {
    asking_.erase(found);
  }
}

void DecisionProbes::OnAnswered(std::string provider_id,
                                std::string model,
                                RemoteModelClient::Outcome outcome) {
  const auto found = asking_.find(provider_id);
  if (found == asking_.end()) {
    return;
  }
  Asking& asking = *found->second;
  asking.out.erase(model);
  if (outcome.ok) {
    const std::optional<base::DictValue> body =
        base::JSONReader::ReadDict(outcome.body, base::JSON_PARSE_RFC);
    Keep(asking.provider, model,
         body && FindCheckAnswer(FormatFor(asking.provider), *body, kQuestion));
  } else if (outcome.http_status == net::HTTP_BAD_REQUEST ||
             outcome.http_status == net::HTTP_NOT_FOUND ||
             outcome.http_status == net::HTTP_UNPROCESSABLE_CONTENT) {
    // The provider turned the model down for decisions.
    Keep(asking.provider, model, false);
  } else {
    const base::TimeTicks now = base::TimeTicks::Now();
    failed_[{provider_id, model}] = now;
    // A rejected key fails every model alike.
    if (outcome.http_status == net::HTTP_UNAUTHORIZED ||
        outcome.http_status == net::HTTP_FORBIDDEN) {
      for (const std::string& each : asking.next) {
        failed_[{provider_id, each}] = now;
      }
      asking.next.clear();
    }
  }
  SendNext(provider_id);
  for (base::OnceClosure& done : std::exchange(waiting_[provider_id], {})) {
    std::move(done).Run();
  }
  waiting_.erase(provider_id);
}

void DecisionProbes::Keep(const Provider& provider,
                          const std::string& model,
                          bool decides) {
  ScopedDictPrefUpdate update(profile_->GetPrefs(), kDecisionProbesPref);
  base::DictValue* entry = update->EnsureDict(provider.id);
  const std::string* address = entry->FindString(kAddress);
  if (!address || *address != provider.address) {
    *entry = base::DictValue().Set(kAddress, provider.address);
  }
  entry->EnsureDict(kModels)->Set(
      model, base::DictValue()
                 .Set(kDecides, decides)
                 .Set(kAt, base::TimeToValue(base::Time::Now())));
}

}  // namespace uw
