// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/browser/ui/views/mini_player/mini_player_prefs.h"

#include <algorithm>

#include "base/values.h"
#include "components/prefs/pref_registry_simple.h"
#include "components/prefs/pref_service.h"
#include "components/prefs/scoped_user_pref_update.h"

namespace uw {

void RegisterMiniPlayerLocalStatePrefs(PrefRegistrySimple* registry) {
  registry->RegisterIntegerPref(kMiniPlayerModePref,
                                static_cast<int>(MiniPlayerMode::kDock));
  registry->RegisterBooleanPref(kMiniPlayerOnTabSwitchPref, true);
  registry->RegisterBooleanPref(kMiniPlayerFloatInBackgroundPref, true);
  registry->RegisterListPref(kMiniPlayerSiteWindowsPref);
}

std::vector<SiteWindowSite> SiteWindowSites(const PrefService* local_state) {
  std::vector<SiteWindowSite> sites;
  if (!local_state) {
    return sites;
  }
  for (const base::Value& item :
       local_state->GetList(kMiniPlayerSiteWindowsPref)) {
    const base::DictValue* dict = item.GetIfDict();
    const std::string* site = dict ? dict->FindString("site") : nullptr;
    if (site) {
      sites.push_back({*site, dict->FindBool("live").value_or(true)});
    }
  }
  return sites;
}

void NoteSiteWindow(PrefService* local_state, const std::string& site) {
  if (!local_state || site.empty()) {
    return;
  }
  const bool live = IsSiteLive(local_state, site);
  ScopedListPrefUpdate update(local_state, kMiniPlayerSiteWindowsPref);
  update->EraseIf([&site](const base::Value& item) {
    const base::DictValue* dict = item.GetIfDict();
    const std::string* stored = dict ? dict->FindString("site") : nullptr;
    return stored && *stored == site;
  });
  update->Insert(
      update->begin(),
      base::Value(base::DictValue().Set("site", site).Set("live", live)));
}

bool IsSiteLive(const PrefService* local_state, const std::string& site) {
  const std::vector<SiteWindowSite> sites = SiteWindowSites(local_state);
  const auto found = std::ranges::find(sites, site, &SiteWindowSite::site);
  return found == sites.end() || found->live;
}

void SetSiteLive(PrefService* local_state, const std::string& site, bool live) {
  if (!local_state || site.empty()) {
    return;
  }
  ScopedListPrefUpdate update(local_state, kMiniPlayerSiteWindowsPref);
  for (base::Value& item : *update) {
    base::DictValue* dict = item.GetIfDict();
    const std::string* stored = dict ? dict->FindString("site") : nullptr;
    if (stored && *stored == site) {
      dict->Set("live", live);
      return;
    }
  }
  update->Append(base::DictValue().Set("site", site).Set("live", live));
}

MiniPlayerMode MiniPlayerModeOf(const PrefService* local_state) {
  const int mode = local_state ? local_state->GetInteger(kMiniPlayerModePref)
                               : static_cast<int>(MiniPlayerMode::kDock);
  return mode >= 0 && mode <= static_cast<int>(MiniPlayerMode::kMaxValue)
             ? static_cast<MiniPlayerMode>(mode)
             : MiniPlayerMode::kDock;
}

}  // namespace uw
