// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/browser/branding/release_version.h"

#include "base/version_info/version_info.h"

namespace uw {

// Only Mac and Windows releases carry a uw version.
std::optional<std::string> ReleaseVersion() {
  return std::nullopt;
}

std::string ChromiumVersion() {
  return std::string(version_info::GetVersionNumber());
}

}  // namespace uw
