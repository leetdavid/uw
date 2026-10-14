// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/browser/terminal/terminal_tab.h"

#include "base/strings/strcat.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "content/public/common/url_constants.h"
#include "url/gurl.h"
#include "url/url_constants.h"

namespace uw {

void OpenTerminalTab(BrowserWindowInterface* window) {
  chrome::AddTabAt(
      window,
      GURL(base::StrCat({content::kChromeUIScheme,
                         url::kStandardSchemeSeparator, kTerminalHost})),
      -1, true);
}

bool IsTerminalUrl(const GURL& url) {
  return url.SchemeIs(content::kChromeUIScheme) && url.host() == kTerminalHost;
}

}  // namespace uw
