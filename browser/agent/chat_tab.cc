// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/browser/agent/chat_tab.h"

#include <memory>
#include <utility>

#include "base/strings/strcat.h"
#include "base/supports_user_data.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/url_constants.h"
#include "url/gurl.h"
#include "url/url_constants.h"

namespace uw {
namespace {

const void* const kStartKey = &kStartKey;

struct StartData : base::SupportsUserData::Data {
  ChatStart start;
};

GURL ChatUrl() {
  return GURL(base::StrCat(
      {content::kChromeUIScheme, url::kStandardSchemeSeparator, kChatHost}));
}

}  // namespace

void OpenChatTab(BrowserWindowInterface* window) {
  chrome::AddTabAt(window, ChatUrl(), -1, true);
}

void AskInNewChat(BrowserWindowInterface* window,
                  tabs::TabInterface* tab,
                  const std::string& text) {
  content::WebContents* chat =
      chrome::AddAndReturnTabAt(window, ChatUrl(), -1, true);
  if (!chat) {
    return;
  }
  auto data = std::make_unique<StartData>();
  data->start.text = text;
  if (tab) {
    data->start.tab = tab->GetHandle();
  }
  chat->SetUserData(kStartKey, std::move(data));
}

std::optional<ChatStart> TakeChatStart(content::WebContents* contents) {
  auto* data = static_cast<StartData*>(contents->GetUserData(kStartKey));
  if (!data) {
    return std::nullopt;
  }
  ChatStart start = std::move(data->start);
  contents->RemoveUserData(kStartKey);
  return start;
}

}  // namespace uw
