// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/browser/agent/chat_tab.h"

#include "base/strings/strcat.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "content/public/common/url_constants.h"
#include "url/gurl.h"
#include "url/url_constants.h"

namespace uw {

void OpenChatTab(BrowserWindowInterface* window) {
  chrome::AddTabAt(
      window,
      GURL(base::StrCat({content::kChromeUIScheme,
                         url::kStandardSchemeSeparator, kChatHost})),
      -1, true);
}

}  // namespace uw
