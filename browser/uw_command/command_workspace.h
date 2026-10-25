// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#ifndef UW_BROWSER_UW_COMMAND_COMMAND_WORKSPACE_H_
#define UW_BROWSER_UW_COMMAND_COMMAND_WORKSPACE_H_

#include <string>
#include <vector>

#include "base/files/file_path.h"
#include "base/functional/callback_forward.h"
#include "base/types/expected.h"
#include "base/values.h"

class BrowserWindowInterface;
class Profile;

namespace uw {

// The workspace a command applies to: the one --workspace names, or the one
// used last. See docs/plans/uw-command.md#workspaces.
struct CommandWorkspace {
  // Empty for the first workspace of `profile`, which may not exist yet.
  std::string id;
  base::FilePath profile;
};

// The workspace `params` names in "workspace", or the one used last. Fails
// with the sentence to show when no workspace, or several, have that name.
base::expected<CommandWorkspace, std::string> ChooseWorkspace(
    const base::DictValue& params);

// Loads the workspace's profile, then gives `loaded` the profile and the
// workspace's id, or null when the profile can't load.
using WorkspaceLoaded =
    base::OnceCallback<void(Profile* profile, const std::string& id)>;
void LoadWorkspace(CommandWorkspace workspace, WorkspaceLoaded loaded);

// The names of the workspaces on `profile`, or of every workspace when it is
// null.
std::vector<std::string> WorkspaceNames(const Profile* profile);

// The front window of `profile` that the user sees, or null.
BrowserWindowInterface* FrontWindow(Profile* profile);

// The front window of `profile`, or a new one, shown and brought forward, for
// what the command opens in uw, such as a connector's sign-in.
BrowserWindowInterface* ShowFrontWindow(Profile* profile);

}  // namespace uw

#endif  // UW_BROWSER_UW_COMMAND_COMMAND_WORKSPACE_H_
