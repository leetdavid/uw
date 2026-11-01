// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/browser/problem_reports/release_keys.h"

namespace uw {

// Only Mac releases carry the keys so far, so other builds send nothing.
ReleaseKeys GetReleaseKeys() {
  return {};
}

}  // namespace uw
