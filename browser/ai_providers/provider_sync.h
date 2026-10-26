// Copyright 2026 The uw Authors. All rights reserved.

#ifndef UW_BROWSER_AI_PROVIDERS_PROVIDER_SYNC_H_
#define UW_BROWSER_AI_PROVIDERS_PROVIDER_SYNC_H_

#include <map>
#include <memory>

#include "base/memory/raw_ptr.h"
#include "base/scoped_multi_source_observation.h"
#include "base/scoped_observation.h"
#include "chrome/browser/profiles/profile_manager_observer.h"
#include "chrome/browser/profiles/profile_observer.h"

class PrefChangeRegistrar;
class PrefRegistrySimple;
class PrefService;
class Profile;
class ProfileManager;

namespace uw {

// Every profile's providers, in local state.
inline constexpr char kSharedProvidersPref[] = "uw.providers.shared";
// The folder names of the profiles that have joined, in local state.
inline constexpr char kJoinedProfilesPref[] = "uw.providers.joined";

void RegisterSharedProviderPrefs(PrefRegistrySimple* registry);

// Keeps the providers of uw://ai/providers the same in every profile. A
// provider added, changed, or removed in one profile changes local state and
// every loaded profile. A profile that loads takes the shared providers. The
// first time, it adds any only it had, so profiles from before keep theirs;
// later, a provider removed while it was not loaded stays removed. Keys are
// in the Keychain under each provider's id, which every profile shares. Each
// profile keeps its own model choices.
class ProviderSync : public ProfileManagerObserver, public ProfileObserver {
 public:
  // Starts with the first window. Later profiles join as they load.
  static void Start();

  ProviderSync(PrefService* local_state, ProfileManager* profile_manager);
  ProviderSync(const ProviderSync&) = delete;
  ProviderSync& operator=(const ProviderSync&) = delete;
  ~ProviderSync() override;

  // ProfileManagerObserver:
  void OnProfileAdded(Profile* profile) override;
  void OnProfileManagerDestroying() override;

  // ProfileObserver:
  void OnProfileWillBeDestroyed(Profile* profile) override;

 private:
  void Join(Profile* profile);
  void OnChanged(Profile* profile);
  // Gives every loaded profile but `except` the shared providers.
  void Spread(Profile* except);

  const raw_ptr<PrefService> local_state_;
  std::map<Profile*, std::unique_ptr<PrefChangeRegistrar>> registrars_;
  // Set while this class writes profiles, so its own writes are not taken
  // for the user's.
  bool spreading_ = false;
  base::ScopedObservation<ProfileManager, ProfileManagerObserver>
      manager_observation_{this};
  base::ScopedMultiSourceObservation<Profile, ProfileObserver>
      profile_observations_{this};
};

}  // namespace uw

#endif  // UW_BROWSER_AI_PROVIDERS_PROVIDER_SYNC_H_
