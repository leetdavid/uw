// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/browser/agent/run_window.h"

#include <map>
#include <set>
#include <utility>
#include <vector>

#include "base/callback_list.h"
#include "base/functional/bind.h"
#include "base/no_destructor.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/create_browser_window.h"
#include "chrome/browser/ui/browser_window/public/global_browser_collection.h"
#include "chrome/browser/ui/navigator/browser_navigator.h"
#include "chrome/browser/ui/navigator/browser_navigator_params.h"
#include "chrome/browser/ui/tabs/tab_enums.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/web_contents.h"
#include "ui/base/page_transition_types.h"
#include "ui/base/window_open_disposition.h"
#include "url/gurl.h"
#include "url/url_constants.h"
#include "uw/browser/agent/run_window_mark.h"

namespace uw {

namespace {

// Unmarks each run window as it closes.
std::map<const BrowserWindowInterface*, base::CallbackListSubscription>&
Closings() {
  static base::NoDestructor<
      std::map<const BrowserWindowInterface*, base::CallbackListSubscription>>
      closings;
  return *closings;
}

BrowserWindowInterface* FindRunWindow(Profile* profile) {
  BrowserWindowInterface* found = nullptr;
  GlobalBrowserCollection::GetInstance()->ForEach(
      [&](BrowserWindowInterface* window) {
        if (IsRunWindow(window) && window->GetProfile() == profile) {
          found = window;
          return false;
        }
        return true;
      });
  return found;
}

BrowserWindowInterface* MakeRunWindow(Profile* profile) {
  BrowserWindowCreateParams params(BrowserWindowInterface::TYPE_NORMAL,
                                   *profile, /*from_user_gesture=*/false);
  params.omit_from_session_restore = true;
  params.should_trigger_session_restore = false;
  BrowserWindowInterface* window = nullptr;
  {
    ScopedMakingRunWindow making;
    window = CreateBrowserWindow(std::move(params));
  }
  if (!window) {
    return nullptr;
  }
  MarkRunWindow(window, true);
  Closings()[window] = window->RegisterBrowserDidClose(
      base::BindRepeating([](BrowserWindowInterface* closed) {
        MarkRunWindow(closed, false);
        Closings().erase(closed);
      }));
  return window;
}

// `run_tab` and every tab opened from it, or from those, in their order in
// its window.
std::vector<tabs::TabInterface*> RunTabs(tabs::TabInterface& run_tab) {
  TabStripModel* tabs = run_tab.GetBrowserWindowInterface()->GetTabStripModel();
  std::set<const tabs::TabInterface*> run = {&run_tab};
  for (size_t before = 0; before != run.size();) {
    before = run.size();
    for (int i = 0; i < tabs->count(); ++i) {
      if (run.contains(tabs->GetOpenerOfTabAt(i))) {
        run.insert(tabs->GetTabAtIndex(i));
      }
    }
  }
  std::vector<tabs::TabInterface*> ordered;
  for (int i = 0; i < tabs->count(); ++i) {
    if (run.contains(tabs->GetTabAtIndex(i))) {
      ordered.push_back(tabs->GetTabAtIndex(i));
    }
  }
  return ordered;
}

}  // namespace

tabs::TabInterface* OpenRunTab(Profile* profile) {
  BrowserWindowInterface* window = FindRunWindow(profile);
  if (!window) {
    window = MakeRunWindow(profile);
  }
  if (!window) {
    return nullptr;
  }
  NavigateParams params(window, GURL(url::kAboutBlankURL),
                        ui::PAGE_TRANSITION_AUTO_TOPLEVEL);
  params.disposition = WindowOpenDisposition::NEW_BACKGROUND_TAB;
  Navigate(&params);
  return params.navigated_or_inserted_contents
             ? tabs::TabInterface::MaybeGetFromContents(
                   params.navigated_or_inserted_contents)
             : nullptr;
}

void CloseRunTabs(tabs::TabInterface& run_tab) {
  TabStripModel* tabs = run_tab.GetBrowserWindowInterface()->GetTabStripModel();
  std::vector<content::WebContents*> closing;
  for (tabs::TabInterface* tab : RunTabs(run_tab)) {
    closing.push_back(tab->GetContents());
  }
  // Nobody sees these tabs, so a page's "Leave site?" question would never be
  // answered: they go without running unload handlers.
  for (content::WebContents* contents : closing) {
    tabs->DetachAndDeleteWebContentsAt(tabs->GetIndexOfWebContents(contents));
  }
}

void BringRunTab(tabs::TabInterface& tab, tabs::TabInterface& next_to) {
  BrowserWindowInterface* from = tab.GetBrowserWindowInterface();
  BrowserWindowInterface* to = next_to.GetBrowserWindowInterface();
  if (!from || !to || from == to || !IsRunWindow(from)) {
    return;
  }
  TabStripModel* source = from->GetTabStripModel();
  TabStripModel* target = to->GetTabStripModel();
  target->InsertDetachedTabAt(
      target->GetIndexOfTab(&next_to) + 1,
      source->DetachTabAtForInsertion(source->GetIndexOfTab(&tab)),
      AddTabTypes::ADD_NONE);
}

void MoveRunTabs(tabs::TabInterface& run_tab, BrowserWindowInterface& to) {
  BrowserWindowInterface* from = run_tab.GetBrowserWindowInterface();
  if (!from || from == &to || !IsRunWindow(from)) {
    return;
  }
  TabStripModel* source = from->GetTabStripModel();
  TabStripModel* target = to.GetTabStripModel();
  for (tabs::TabInterface* tab : RunTabs(run_tab)) {
    target->InsertDetachedTabAt(
        target->count(),
        source->DetachTabAtForInsertion(source->GetIndexOfTab(tab)),
        AddTabTypes::ADD_NONE);
  }
}

}  // namespace uw
