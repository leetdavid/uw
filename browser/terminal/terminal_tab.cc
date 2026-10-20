// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/browser/terminal/terminal_tab.h"

#include <memory>
#include <utility>
#include <vector>

#include "base/files/file_path.h"
#include "base/strings/strcat.h"
#include "base/strings/utf_string_conversions.h"
#include "base/supports_user_data.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "content/public/browser/invalidate_type.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/drop_data.h"
#include "content/public/common/url_constants.h"
#include "ui/base/clipboard/file_info.h"
#include "url/gurl.h"
#include "url/url_constants.h"
#include "uw/components/terminal/shell_quote.h"

namespace uw {
namespace {

const void* const kFolderKey = &kFolderKey;
const void* const kStartCommandKey = &kStartCommandKey;
const void* const kAgentKey = &kAgentKey;

struct FolderData : base::SupportsUserData::Data {
  base::FilePath folder;
};

struct StartCommandData : base::SupportsUserData::Data {
  StartCommand start;
};

}  // namespace

GURL TerminalUrl() {
  return GURL(base::StrCat({content::kChromeUIScheme,
                            url::kStandardSchemeSeparator, kTerminalHost}));
}

void OpenTerminalTab(BrowserWindowInterface* window) {
  chrome::AddTabAt(window, TerminalUrl(), -1, true);
}

StartCommand::StartCommand() = default;
StartCommand::StartCommand(StartCommand&&) = default;
StartCommand& StartCommand::operator=(StartCommand&&) = default;
StartCommand::~StartCommand() = default;

void OpenTerminalTabRunning(BrowserWindowInterface* window,
                            const std::string& command,
                            base::EnvironmentMap environment) {
  content::WebContents* contents =
      chrome::AddAndReturnTabAt(window, TerminalUrl(), -1, true);
  if (!contents) {
    return;
  }
  auto data = std::make_unique<StartCommandData>();
  data->start.command = command;
  data->start.environment = std::move(environment);
  contents->SetUserData(kStartCommandKey, std::move(data));
}

StartCommand TakeStartCommand(content::WebContents* contents) {
  auto* data =
      static_cast<StartCommandData*>(contents->GetUserData(kStartCommandKey));
  if (!data) {
    return StartCommand();
  }
  StartCommand start = std::move(data->start);
  contents->RemoveUserData(kStartCommandKey);
  return start;
}

bool IsTerminalUrl(const GURL& url) {
  return url.SchemeIs(content::kChromeUIScheme) && url.host() == kTerminalHost;
}

void MarkAsAgentTerminal(content::WebContents* contents) {
  contents->SetUserData(kAgentKey,
                        std::make_unique<base::SupportsUserData::Data>());
}

bool IsAgentTerminal(content::WebContents* contents) {
  return contents->GetUserData(kAgentKey) &&
         IsTerminalUrl(contents->GetLastCommittedURL());
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

void DropFilesAsPaths(content::WebContents* contents, content::DropData& drop) {
  if (drop.filenames.empty() ||
      !IsTerminalUrl(contents->GetLastCommittedURL())) {
    return;
  }
  std::vector<base::FilePath> paths;
  for (const ui::FileInfo& file : drop.filenames) {
    paths.push_back(file.path);
  }
  drop.text = base::UTF8ToUTF16(QuotePathsForShell(paths));
  drop.filenames.clear();
  drop.file_mime_types.clear();
  drop.file_system_files.clear();
}

}  // namespace uw
