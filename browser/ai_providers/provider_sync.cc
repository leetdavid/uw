// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/browser/ai_providers/provider_sync.h"

#include <algorithm>
#include <string>
#include <utility>

#include "base/auto_reset.h"
#include "base/functional/bind.h"
#include "base/no_destructor.h"
#include "base/values.h"
#include "chrome/browser/browser_process.h"  // nogncheck
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "components/prefs/pref_change_registrar.h"
#include "components/prefs/pref_registry_simple.h"
#include "components/prefs/pref_service.h"
#include "components/prefs/scoped_user_pref_update.h"
#include "uw/components/ai_providers/providers.h"

namespace uw {
namespace {

const std::string* IdOf(const base::Value& entry) {
  const base::DictValue* provider = entry.GetIfDict();
  return provider ? provider->FindString("id") : nullptr;
}

bool Has(const base::ListValue& providers, const std::string& id) {
  return std::ranges::any_of(providers, [&id](const base::Value& entry) {
    const std::string* other = IdOf(entry);
    return other && *other == id;
  });
}

}  // namespace

void RegisterSharedProviderPrefs(PrefRegistrySimple* registry) {
  registry->RegisterListPref(kSharedProvidersPref);
  registry->RegisterListPref(kJoinedProfilesPref);
}

// static
void ProviderSync::Start() {
  static base::NoDestructor<ProviderSync> sync(
      g_browser_process->local_state(), g_browser_process->profile_manager());
}

ProviderSync::ProviderSync(PrefService* local_state,
                           ProfileManager* profile_manager)
    : local_state_(local_state) {
  manager_observation_.Observe(profile_manager);
  for (Profile* profile : profile_manager->GetLoadedProfiles()) {
    Join(profile);
  }
}

ProviderSync::~ProviderSync() = default;

void ProviderSync::OnProfileAdded(Profile* profile) {
  Join(profile);
}

void ProviderSync::OnProfileManagerDestroying() {
  registrars_.clear();
  profile_observations_.RemoveAllObservations();
  manager_observation_.Reset();
}

void ProviderSync::OnProfileWillBeDestroyed(Profile* profile) {
  registrars_.erase(profile);
  profile_observations_.RemoveObservation(profile);
}

void ProviderSync::Join(Profile* profile) {
  if (profile->IsOffTheRecord() || profile->IsGuestSession() ||
      profile->IsSystemProfile() || registrars_.contains(profile)) {
    return;
  }
  PrefService* prefs = profile->GetPrefs();
  base::ListValue shared = local_state_->GetList(kSharedProvidersPref).Clone();
  bool grew = false;
  const std::string name = profile->GetBaseName().AsUTF8Unsafe();
  if (!local_state_->GetList(kJoinedProfilesPref).contains(name)) {
    for (const base::Value& own : prefs->GetList(kProvidersPref)) {
      if (const std::string* id = IdOf(own); id && !Has(shared, *id)) {
        shared.Append(own.Clone());
        grew = true;
      }
    }
    ScopedListPrefUpdate(local_state_, kJoinedProfilesPref)->Append(name);
  }
  local_state_->SetList(kSharedProvidersPref, shared.Clone());
  {
    base::AutoReset<bool> spreading(&spreading_, true);
    prefs->SetList(kProvidersPref, std::move(shared));
  }
  auto registrar = std::make_unique<PrefChangeRegistrar>();
  registrar->Init(prefs);
  registrar->Add(kProvidersPref,
                 base::BindRepeating(&ProviderSync::OnChanged,
                                     base::Unretained(this), profile));
  registrars_[profile] = std::move(registrar);
  profile_observations_.AddObservation(profile);
  if (grew) {
    Spread(profile);
  }
}

void ProviderSync::OnChanged(Profile* profile) {
  if (spreading_) {
    return;
  }
  local_state_->SetList(kSharedProvidersPref,
                        profile->GetPrefs()->GetList(kProvidersPref).Clone());
  Spread(profile);
}

void ProviderSync::Spread(Profile* except) {
  base::AutoReset<bool> spreading(&spreading_, true);
  const base::ListValue& shared = local_state_->GetList(kSharedProvidersPref);
  for (const auto& [profile, registrar] : registrars_) {
    if (profile != except) {
      profile->GetPrefs()->SetList(kProvidersPref, shared.Clone());
    }
  }
}

}  // namespace uw
