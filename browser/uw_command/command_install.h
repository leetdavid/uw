// Copyright 2026 The uw Authors. All rights reserved.

#ifndef UW_BROWSER_UW_COMMAND_COMMAND_INSTALL_H_
#define UW_BROWSER_UW_COMMAND_COMMAND_INSTALL_H_

#include "base/files/file_path.h"

namespace uw {

// Where "Install uw command" links the command. See
// docs/plans/uw-command.md#reaching-uw.
inline constexpr char kCommandLink[] = "/usr/local/bin/uw";

// Where the command is linked: kCommandLink, or the folder a test gave.
base::FilePath CommandLink();
void SetCommandLinkForTesting(const base::FilePath& link);

// The command inside this uw: uw.app/Contents/Helpers/uw.
base::FilePath CommandBinary();

// Whether `link` points into `app`, so the command there belongs to it. Blocks
// on the disk.
bool LinksInto(const base::FilePath& link, const base::FilePath& app);

// Whether CommandLink() leads into this uw. Blocks on the disk.
bool CommandInstalled();

// How installing or uninstalling went.
enum class CommandInstall { kDone, kCancelled, kFailed };

// Links `link` to `binary`. When the user can't change the link's folder,
// macOS asks for an administrator's password first. Blocks until done.
CommandInstall LinkCommand(const base::FilePath& link,
                           const base::FilePath& binary);

// Removes `link` the same way.
CommandInstall UnlinkCommand(const base::FilePath& link);

}  // namespace uw

#endif  // UW_BROWSER_UW_COMMAND_COMMAND_INSTALL_H_
