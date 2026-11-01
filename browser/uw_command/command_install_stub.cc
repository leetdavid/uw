// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/browser/uw_command/command_install.h"

#include "base/files/file_path.h"

// Installing the command works on the Mac only so far; see
// docs/plans/windows.md.

namespace uw {

base::FilePath CommandLink() {
  return base::FilePath();
}

void SetCommandLinkForTesting(const base::FilePath& link) {}

base::FilePath CommandBinary() {
  return base::FilePath();
}

bool LinksInto(const base::FilePath& link, const base::FilePath& app) {
  return false;
}

bool CommandInstalled() {
  return false;
}

CommandInstall LinkCommand(const base::FilePath& link,
                           const base::FilePath& binary) {
  return CommandInstall::kFailed;
}

CommandInstall UnlinkCommand(const base::FilePath& link) {
  return CommandInstall::kFailed;
}

}  // namespace uw
