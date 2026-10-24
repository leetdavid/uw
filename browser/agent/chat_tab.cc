// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/browser/agent/chat_tab.h"

#include <memory>
#include <utility>

#include "base/strings/strcat.h"
#include "base/supports_user_data.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/tab_list/tab_list_interface.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface_iterator.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/url_constants.h"
#include "ui/base/base_window.h"
#include "url/gurl.h"
#include "url/url_constants.h"

namespace uw {
namespace {

const void* const kStartKey = &kStartKey;

struct StartData : base::SupportsUserData::Data {
  ChatStart start;
};

}  // namespace

GURL ChatUrl(std::string_view id) {
  return GURL(
      base::StrCat({content::kChromeUIScheme, url::kStandardSchemeSeparator,
                    kChatHost, "/", id}));
}

std::vector<tabs::TabInterface*> TabsShowingChat(Profile* profile,
                                                 std::string_view id) {
  const GURL url = ChatUrl(id);
  std::vector<tabs::TabInterface*> showing;
  for (BrowserWindowInterface* window : GetAllBrowserWindowInterfaces()) {
    if (window->GetProfile() != profile) {
      continue;
    }
    for (tabs::TabInterface* tab :
         TabListInterface::From(window)->GetAllTabs()) {
      if (tab->GetContents()->GetVisibleURL() == url) {
        showing.push_back(tab);
      }
    }
  }
  return showing;
}

tabs::TabInterface* LastChatTab(BrowserWindowInterface* window) {
  tabs::TabInterface* last = nullptr;
  for (tabs::TabInterface* tab :
       TabListInterface::From(window)->GetAllTabs()) {
    const GURL& url = tab->GetContents()->GetVisibleURL();
    if (url.SchemeIs(content::kChromeUIScheme) && url.host() == kChatHost &&
        (!last || tab->GetContents()->GetLastActiveTimeTicks() >
                      last->GetContents()->GetLastActiveTimeTicks())) {
      last = tab;
    }
  }
  return last;
}

void GoToTab(tabs::TabInterface* tab) {
  BrowserWindowInterface* window = tab->GetBrowserWindowInterface();
  TabListInterface::From(window)->ActivateTab(tab->GetHandle());
  window->GetWindow()->Activate();
}

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
