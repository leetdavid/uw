// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/browser/uw_command/command_install.h"

#include "base/apple/bundle_locations.h"
#include "base/files/file_util.h"

namespace uw {

base::FilePath CommandBinary() {
  return base::apple::OuterBundlePath().Append("Contents/Helpers/uw");
}

bool LinksInto(const base::FilePath& link, const base::FilePath& app) {
  const base::FilePath target = base::MakeAbsoluteFilePath(link);
  const base::FilePath bundle = base::MakeAbsoluteFilePath(app);
  return !target.empty() && !bundle.empty() && bundle.IsParent(target);
}

}  // namespace uw
