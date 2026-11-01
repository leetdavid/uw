// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/browser/branding/release_version.h"

namespace uw {

// Only Mac releases carry a uw version so far.
std::optional<std::string> ReleaseVersion() {
  return std::nullopt;
}

}  // namespace uw
