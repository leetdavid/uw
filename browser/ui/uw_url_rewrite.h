// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#ifndef UW_BROWSER_UI_UW_URL_REWRITE_H_
#define UW_BROWSER_UI_UW_URL_REWRITE_H_

#include "url/gurl.h"

namespace content {
class BrowserContext;
}

namespace uw {

// The uw scheme aliases the chrome scheme, so uw://settings behaves as
// chrome://settings and every other uw:// URL maps to its chrome://
// counterpart. Add as a BrowserURLHandler pair; returning false lets later
// handlers (including WebUI) process the rewritten URL.
inline constexpr char kUwScheme[] = "uw";

bool HandleUwSchemeRewrite(GURL* url, content::BrowserContext* context);

}  // namespace uw

#endif  // UW_BROWSER_UI_UW_URL_REWRITE_H_
