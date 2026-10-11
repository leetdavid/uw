// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/browser/ui/uw_url_rewrite.h"

#include "content/public/common/url_constants.h"

namespace uw {

bool HandleUwSchemeRewrite(GURL* url, content::BrowserContext* /*context*/) {
  if (!url->SchemeIs(kUwScheme)) {
    return false;
  }
  GURL::Replacements replacements;
  replacements.SetSchemeStr(content::kChromeUIScheme);
  *url = url->ReplaceComponents(replacements);
  return false;
}

}  // namespace uw
