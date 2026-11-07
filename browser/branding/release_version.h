// Copyright 2026 The uw Authors. All rights reserved.

#ifndef UW_BROWSER_BRANDING_RELEASE_VERSION_H_
#define UW_BROWSER_BRANDING_RELEASE_VERSION_H_

#include <optional>
#include <string>

namespace uw {

// uw's own version, such as "0.1.0", which `pnpm run release` writes into the
// Mac app bundle and a Windows build's arguments. A development build has none.
std::optional<std::string> ReleaseVersion();

// Chromium's own version, such as "153.0.8010.53". A Windows release's build
// has a four-part version of its own made from it, such as 153.0.8010.5312.
// See docs/adr/0027-windows-uw-numbers-its-releases-from-chromiums-version.md.
std::string ChromiumVersion();

}  // namespace uw

#endif  // UW_BROWSER_BRANDING_RELEASE_VERSION_H_
