// Copyright 2026 The uw Authors. All rights reserved.

#ifndef UW_BROWSER_PROBLEM_REPORTS_RELEASE_KEYS_H_
#define UW_BROWSER_PROBLEM_REPORTS_RELEASE_KEYS_H_

#include <string>

namespace uw {

// Where a release sends its reports. `pnpm run release` writes these into the
// app bundle's Info.plist; a development build carries none and sends nothing.
// See docs/plans/distribution.md, "Problem reports".
struct ReleaseKeys {
  // Sentry's minidump address, which Crashpad uploads crash dumps to.
  std::string crash_url;
  // PostHog's project key and host, which uw's pages report problems to.
  std::string posthog_key;
  std::string posthog_host;

  bool available() const { return !crash_url.empty(); }
};

ReleaseKeys GetReleaseKeys();

}  // namespace uw

#endif  // UW_BROWSER_PROBLEM_REPORTS_RELEASE_KEYS_H_
