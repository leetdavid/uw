// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/browser/branding/release_version.h"

#include <string>
#include <vector>

#include "base/strings/stringprintf.h"
#include "base/version.h"
#include "base/version_info/version_info.h"
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

std::string ChromiumVersion() {
  const base::Version& build = version_info::GetVersion();
  if (!ReleaseVersion()) {
    return build.GetString();
  }
  const std::vector<uint32_t>& parts = build.components();
  return base::StringPrintf("%u.%u.%u.%u", parts[0], parts[1], parts[2],
                            parts[3] / 100);
}

}  // namespace uw
