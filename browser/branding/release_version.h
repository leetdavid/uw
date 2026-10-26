// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#ifndef UW_BROWSER_BRANDING_RELEASE_VERSION_H_
#define UW_BROWSER_BRANDING_RELEASE_VERSION_H_

#include <optional>
#include <string>

namespace uw {

// uw's own version, such as "0.1.0", which `pnpm run release` writes into the
// app bundle. A development build carries Chromium's version there and has
// none.
std::optional<std::string> ReleaseVersion();

}  // namespace uw

#endif  // UW_BROWSER_BRANDING_RELEASE_VERSION_H_
