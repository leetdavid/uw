// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#ifndef UW_BROWSER_PINS_PIN_PAGES_H_
#define UW_BROWSER_PINS_PIN_PAGES_H_

#include <map>
#include <memory>
#include <string>
#include <string_view>

#include "base/callback_list.h"
#include "base/memory/raw_ptr.h"
#include "base/scoped_observation.h"
#include "base/supports_user_data.h"
#include "chrome/browser/profiles/profile_manager_observer.h"
#include "chrome/browser/profiles/profile_observer.h"

class Profile;
class ProfileManager;
class ScopedProfileKeepAlive;

namespace content {
class WebContents;
}

namespace uw {

// The live pages of one profile's pins while no window holds them, as after
// the window that showed one closed. They stay alive, and keep the profile
// alive, until a window takes them, the profile goes, or uw quits.
class PinPages : public base::SupportsUserData::Data,
                 public ProfileObserver,
                 public ProfileManagerObserver {
 public:
  // Null for an off-the-record profile, which keeps no pins of its own.
  static PinPages* For(Profile* profile);

  explicit PinPages(Profile* profile);
  PinPages(const PinPages&) = delete;
  PinPages& operator=(const PinPages&) = delete;
  ~PinPages() override;

  void Hold(std::string id, std::unique_ptr<content::WebContents> page);
  // The page of pin `id`, which the caller then holds, or null.
  std::unique_ptr<content::WebContents> Take(std::string_view id);
  bool Holds(std::string_view id) const;

  // ProfileObserver:
  void OnProfileWillBeDestroyed(Profile* profile) override;
  // ProfileManagerObserver:
  void OnProfileMarkedForPermanentDeletion(Profile* profile) override;
  void OnProfileManagerDestroying() override;

 private:
  void DropAll();

  const raw_ptr<Profile> profile_;
  std::map<std::string, std::unique_ptr<content::WebContents>, std::less<>>
      pages_;
  std::unique_ptr<ScopedProfileKeepAlive> keep_alive_;
  base::CallbackListSubscription terminating_;
  base::ScopedObservation<Profile, ProfileObserver> profile_observation_{this};
  base::ScopedObservation<ProfileManager, ProfileManagerObserver>
      manager_observation_{this};
};

}  // namespace uw

#endif  // UW_BROWSER_PINS_PIN_PAGES_H_
