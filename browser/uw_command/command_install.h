// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#ifndef UW_BROWSER_UW_COMMAND_COMMAND_INSTALL_H_
#define UW_BROWSER_UW_COMMAND_COMMAND_INSTALL_H_

#include "base/files/file_path.h"

namespace uw {

// Where "Install uw command" links the command. See
// docs/plans/uw-command.md#reaching-uw.
inline constexpr char kCommandLink[] = "/usr/local/bin/uw";

// The command inside this uw: uw.app/Contents/Helpers/uw.
base::FilePath CommandBinary();

// Whether `link` leads into `app`, so the command there belongs to it. Blocks
// on the disk.
bool LinksInto(const base::FilePath& link, const base::FilePath& app);

}  // namespace uw

#endif  // UW_BROWSER_UW_COMMAND_COMMAND_INSTALL_H_
