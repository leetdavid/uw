// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/browser/branding/release_version.h"

#include <string>

#include "build/buildflag.h"
#include "uw/browser/branding/release_buildflags.h"

namespace uw {

std::optional<std::string> ReleaseVersion() {
  std::string version = BUILDFLAG(UW_RELEASE_VERSION);
  if (version.empty()) {
    return std::nullopt;
  }
  return version;
}

}  // namespace uw
