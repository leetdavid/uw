// Copyright 2026 The uw Authors. All rights reserved.

#ifndef UW_BROWSER_UI_VIEWS_WORKSPACES_WORKSPACE_TEXT_H_
#define UW_BROWSER_UI_VIEWS_WORKSPACES_WORKSPACE_TEXT_H_

#include <string>
#include <string_view>
#include <vector>

#include "base/files/file_path.h"

namespace uw {

// `names` as the locale writes a list, as in "Personal and Reading".
std::u16string JoinNames(const std::vector<std::u16string>& names);
// The name of the profile in folder `profile`.
std::u16string ProfileName(const base::FilePath& profile);
// The names of the workspaces on `profile`, besides `except`.
std::vector<std::u16string> WorkspaceNamesOn(const base::FilePath& profile,
                                             std::string_view except = {});
// The emoji of the workspaces on `profile`, run together.
std::u16string WorkspaceIconsOn(const base::FilePath& profile);

}  // namespace uw

#endif  // UW_BROWSER_UI_VIEWS_WORKSPACES_WORKSPACE_TEXT_H_
