// Copyright 2026 The uw Authors. All rights reserved.

#ifndef UW_BROWSER_WORKSPACES_WORKSPACE_PREFS_H_
#define UW_BROWSER_WORKSPACES_WORKSPACE_PREFS_H_

class PrefRegistrySimple;

namespace uw {

// Every workspace in the switcher's order, in local state: a list of
// {"id", "name", "icon", "profile"}, where "profile" is the profile's
// directory name.
inline constexpr char kWorkspacesPref[] = "uw.workspaces";

void RegisterWorkspaceLocalStatePrefs(PrefRegistrySimple* registry);

}  // namespace uw

#endif  // UW_BROWSER_WORKSPACES_WORKSPACE_PREFS_H_
