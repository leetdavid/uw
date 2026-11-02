// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/browser/problem_reports/release_keys.h"

#include "build/buildflag.h"
#include "uw/browser/branding/release_buildflags.h"

namespace uw {

ReleaseKeys GetReleaseKeys() {
  return {
      .crash_url = BUILDFLAG(UW_CRASH_REPORT_URL),
      .posthog_key = BUILDFLAG(UW_POSTHOG_KEY),
      .posthog_host = BUILDFLAG(UW_POSTHOG_HOST),
  };
}

}  // namespace uw
