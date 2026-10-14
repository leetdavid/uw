// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/browser/terminal/terminal_tab.h"

#include <memory>
#include <utility>

#include "base/files/file_path.h"
#include "base/strings/strcat.h"
#include "base/supports_user_data.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "content/public/browser/invalidate_type.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/url_constants.h"
#include "url/gurl.h"
#include "url/url_constants.h"

namespace uw {
namespace {

const void* const kFolderKey = &kFolderKey;

struct FolderData : base::SupportsUserData::Data {
  base::FilePath folder;
};

}  // namespace

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

base::FilePath TerminalFolder(content::WebContents* contents) {
  const auto* data =
      static_cast<const FolderData*>(contents->GetUserData(kFolderKey));
  return data ? data->folder : base::FilePath();
}

void SetTerminalFolder(content::WebContents* contents,
                       const base::FilePath& folder) {
  auto data = std::make_unique<FolderData>();
  data->folder = folder;
  contents->SetUserData(kFolderKey, std::move(data));
  // The tab strip passes the change on, so the organizer looks at the tab
  // again even when a title the shell set stays the same.
  contents->NotifyNavigationStateChanged(content::INVALIDATE_TYPE_TAB);
}

}  // namespace uw
