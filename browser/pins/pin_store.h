// Copyright 2026 The uw Authors. All rights reserved.

#ifndef UW_BROWSER_PINS_PIN_STORE_H_
#define UW_BROWSER_PINS_PIN_STORE_H_

#include <string>
#include <string_view>
#include <vector>

#include "base/callback_list.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/supports_user_data.h"
#include "url/gurl.h"

class PrefService;
class Profile;

namespace uw {

// The pins of one profile, in sidebar order, each with the home URL it returns
// to when reset. Saved in the profile's prefs. Every window of the profile
// shows them; see PinSync.
class PinStore : public base::SupportsUserData::Data {
 public:
  struct Pin {
    std::string id;
    GURL home;
  };

  // Null for an off-the-record profile, which keeps no pins of its own.
  static PinStore* For(Profile* profile);

  explicit PinStore(PrefService* prefs);
  PinStore(const PinStore&) = delete;
  PinStore& operator=(const PinStore&) = delete;
  ~PinStore() override;

  const std::vector<Pin>& pins() const { return pins_; }
  const Pin* Find(std::string_view id) const;

  // Each change saves the pins and tells the observers on a later task, so no
  // window's tab strip changes inside another's notification.
  std::string Insert(size_t index, const GURL& home);
  void Remove(std::string_view id);
  // Puts the pins named in `ids` in that order, ahead of any others.
  void Reorder(const std::vector<std::string>& ids);
  void SetHome(std::string_view id, const GURL& home);

  base::CallbackListSubscription Observe(base::RepeatingClosure changed);

 private:
  void Changed();
  void Notify();

  const raw_ptr<PrefService> prefs_;
  std::vector<Pin> pins_;
  bool notify_pending_ = false;
  base::RepeatingClosureList observers_;
  base::WeakPtrFactory<PinStore> weak_factory_{this};
};

}  // namespace uw

#endif  // UW_BROWSER_PINS_PIN_STORE_H_
