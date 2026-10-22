// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/browser/pins/pin_sync.h"

#include <algorithm>
#include <utility>
#include <vector>

#include "base/auto_reset.h"
#include "base/containers/adapters.h"
#include "base/functional/bind.h"
#include "base/task/sequenced_task_runner.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/sessions/session_service.h"
#include "chrome/browser/sessions/session_service_factory.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/navigator/browser_navigator.h"
#include "chrome/browser/ui/navigator/browser_navigator_params.h"
#include "chrome/browser/ui/tabs/tab_enums.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "components/sessions/content/session_tab_helper.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_user_data.h"
#include "content/public/common/referrer.h"
#include "ui/base/page_transition_types.h"
#include "ui/base/window_open_disposition.h"

namespace uw {
namespace {

// The pin a tab shows. It stays with the page when the tab moves to another
// window. `restored` is set for a mark read back from the session, until the
// window next applies the store.
class PinMark : public content::WebContentsUserData<PinMark> {
 public:
  ~PinMark() override = default;

  const std::string& id() const { return id_; }
  bool restored() const { return restored_; }

 private:
  friend WebContentsUserData;
  PinMark(content::WebContents* page, std::string id, bool restored)
      : WebContentsUserData(*page), id_(std::move(id)), restored_(restored) {}

  const std::string id_;
  const bool restored_;
  WEB_CONTENTS_USER_DATA_KEY_DECL();
};

WEB_CONTENTS_USER_DATA_KEY_IMPL(PinMark);

const PinMark* MarkOf(content::WebContents* page) {
  return page ? PinMark::FromWebContents(page) : nullptr;
}

void Mark(content::WebContents* page, std::string id, bool restored = false) {
  page->RemoveUserData(PinMark::UserDataKey());
  PinMark::CreateForWebContents(page, std::move(id), restored);
}

void Unmark(content::WebContents* page) {
  page->RemoveUserData(PinMark::UserDataKey());
}

GURL CurrentUrl(content::WebContents* page) {
  const GURL& committed = page->GetLastCommittedURL();
  return committed.is_valid() ? committed : page->GetVisibleURL();
}

PinStore* StoreFor(content::WebContents* page) {
  return PinStore::For(Profile::FromBrowserContext(page->GetBrowserContext()));
}

void Load(const tabs::TabInterface* tab, const GURL& url) {
  tab->GetContents()->GetController().LoadURL(
      url, content::Referrer(), ui::PAGE_TRANSITION_AUTO_BOOKMARK,
      std::string());
}

}  // namespace

DEFINE_USER_DATA(PinSync);

PinSync::PinSync(BrowserWindowInterface* window)
    : window_(window),
      tabs_(window->GetTabStripModel()),
      store_(PinStore::For(window->GetProfile())),
      user_data_(window->GetUnownedUserDataHost(), *this) {
  if (!store_) {
    return;
  }
  tabs_->AddObserver(this);
  store_subscription_ = store_->Observe(
      base::BindRepeating(&PinSync::ScheduleApply, base::Unretained(this)));
  // Later, so a restored window's pins arrive before this window adds any.
  ScheduleApply();
}

PinSync::~PinSync() {
  if (store_) {
    tabs_->RemoveObserver(this);
  }
}

// static
PinSync* PinSync::From(BrowserWindowInterface* window) {
  return window ? Get(window->GetUnownedUserDataHost()) : nullptr;
}

// static
void PinSync::RestoreTab(
    content::WebContents* page,
    const std::map<std::string, std::string>& extra_data) {
  const auto saved = extra_data.find(kSessionKey);
  if (page && saved != extra_data.end() && !saved->second.empty()) {
    Mark(page, saved->second, /*restored=*/true);
  }
}

// static
const PinStore::Pin* PinSync::PinFor(const tabs::TabInterface* tab) {
  if (!tab || !tab->IsPinned()) {
    return nullptr;
  }
  content::WebContents* page = tab->GetContents();
  const PinMark* mark = MarkOf(page);
  PinStore* store = mark ? StoreFor(page) : nullptr;
  return store ? store->Find(mark->id()) : nullptr;
}

// static
bool PinSync::KeepsActiveTab(BrowserWindowInterface* window) {
  return PinFor(window->GetTabStripModel()->GetActiveTab());
}

// static
bool PinSync::CanClose(const tabs::TabInterface* tab) {
  return !PinFor(tab);
}

// static
void PinSync::ResetToHome(const tabs::TabInterface* tab) {
  if (const PinStore::Pin* pin = PinFor(tab)) {
    Load(tab, pin->home);
  }
}

// static
void PinSync::ReplaceHomeWithCurrent(const tabs::TabInterface* tab) {
  if (const PinStore::Pin* pin = PinFor(tab)) {
    StoreFor(tab->GetContents())
        ->SetHome(pin->id, CurrentUrl(tab->GetContents()));
  }
}

// static
void PinSync::SetHome(const tabs::TabInterface* tab, const GURL& home) {
  const PinStore::Pin* pin = PinFor(tab);
  if (!pin || !home.is_valid()) {
    return;
  }
  StoreFor(tab->GetContents())->SetHome(pin->id, home);
  Load(tab, home);
}

void PinSync::OnTabStripModelChanged(TabStripModel* model,
                                     const TabStripModelChange& change,
                                     const TabStripSelectionChange& selection) {
  if (applying_) {
    return;
  }
  switch (change.type()) {
    case TabStripModelChange::kInserted:
      if (std::ranges::any_of(change.GetInsert()->contents,
                              [](const auto& added) {
                                return added.tab->IsPinned();
                              })) {
        ScheduleApply();
      }
      break;
    case TabStripModelChange::kRemoved:
      // A pin that left this window, closed or dragged away, comes back.
      if (!tabs_->closing_all()) {
        ScheduleApply();
      }
      break;
    case TabStripModelChange::kMoved:
      if (change.GetMove()->tab->IsPinned()) {
        ReportOrder();
      }
      break;
    case TabStripModelChange::kReplaced: {
      const TabStripModelChange::Replace* replace = change.GetReplace();
      if (const PinMark* mark = MarkOf(replace->old_contents)) {
        Mark(replace->new_contents, mark->id());
      }
      break;
    }
    case TabStripModelChange::kSelectionOnly:
      break;
  }
}

void PinSync::OnTabPinnedStateChanged(tabs::TabInterface* tab, int index) {
  if (applying_) {
    return;
  }
  content::WebContents* page = tab->GetContents();
  if (tab->IsPinned()) {
    Mark(page, store_->Insert(index, CurrentUrl(page)));
  } else if (const PinMark* mark = MarkOf(page)) {
    store_->Remove(mark->id());
    Unmark(page);
  }
  SaveSession();
}

void PinSync::ScheduleApply() {
  if (apply_pending_) {
    return;
  }
  apply_pending_ = true;
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(&PinSync::Apply, weak_factory_.GetWeakPtr()));
}

void PinSync::Apply() {
  apply_pending_ = false;
  if (tabs_->closing_all() || tabs_->empty()) {
    return;
  }
  base::AutoReset<bool> applying(&applying_, true);

  // Keep one copy of each pin: a restored one first, then the selected one.
  std::map<std::string, int> keep;
  for (int i = 0; i < tabs_->IndexOfFirstNonPinnedTab(); ++i) {
    const PinMark* mark = MarkOf(tabs_->GetWebContentsAt(i));
    if (!mark) {
      continue;
    }
    const auto [kept, added] = keep.try_emplace(mark->id(), i);
    const PinMark* other = MarkOf(tabs_->GetWebContentsAt(kept->second));
    if (!added && !other->restored() &&
        (mark->restored() || i == tabs_->active_index())) {
      kept->second = i;
    }
  }
  std::vector<int> closing;
  for (int i = 0; i < tabs_->IndexOfFirstNonPinnedTab(); ++i) {
    const PinMark* mark = MarkOf(tabs_->GetWebContentsAt(i));
    if (!mark) {
      continue;
    }
    // A copy of a pin another window unpinned goes. A restored pin the
    // store has lost is adopted below.
    const bool unpinned = !store_->Find(mark->id()) && !mark->restored();
    if (keep[mark->id()] != i || unpinned) {
      closing.push_back(i);
    }
  }
  for (int index : base::Reversed(closing)) {
    tabs_->CloseWebContentsAt(index, TabCloseTypes::CLOSE_NONE);
  }

  // Adopt pinned tabs the store does not know: restored ones, and ones pinned
  // before uw kept pins.
  for (int i = 0; i < tabs_->IndexOfFirstNonPinnedTab(); ++i) {
    content::WebContents* page = tabs_->GetWebContentsAt(i);
    const PinMark* mark = MarkOf(page);
    if (!mark || !store_->Find(mark->id())) {
      Mark(page, store_->Insert(i, CurrentUrl(page)));
    }
  }

  const std::vector<PinStore::Pin> pins = store_->pins();
  for (int index = 0; index < static_cast<int>(pins.size()); ++index) {
    int at = -1;
    for (int i = 0; i < tabs_->IndexOfFirstNonPinnedTab(); ++i) {
      const PinMark* mark = MarkOf(tabs_->GetWebContentsAt(i));
      if (mark && mark->id() == pins[index].id) {
        at = i;
        break;
      }
    }
    if (at == index) {
      continue;
    }
    if (at >= 0) {
      tabs_->MoveWebContentsAt(at, index, /*select_after_move=*/false);
      continue;
    }
    NavigateParams params(window_, pins[index].home,
                          ui::PAGE_TRANSITION_AUTO_BOOKMARK);
    params.disposition = WindowOpenDisposition::NEW_BACKGROUND_TAB;
    params.tabstrip_index = index;
    params.tabstrip_add_types = AddTabTypes::ADD_PINNED;
    Navigate(&params);
    if (params.navigated_or_inserted_contents) {
      Mark(params.navigated_or_inserted_contents, pins[index].id);
    }
  }

  // From now on a restored pin that leaves the store was unpinned elsewhere,
  // so its copy here goes instead of coming back as a new pin.
  for (int i = 0; i < tabs_->IndexOfFirstNonPinnedTab(); ++i) {
    content::WebContents* page = tabs_->GetWebContentsAt(i);
    if (const PinMark* mark = MarkOf(page); mark && mark->restored()) {
      Mark(page, mark->id());
    }
  }
  SaveSession();
}

void PinSync::ReportOrder() {
  std::vector<std::string> ids;
  for (int i = 0; i < tabs_->IndexOfFirstNonPinnedTab(); ++i) {
    if (const PinMark* mark = MarkOf(tabs_->GetWebContentsAt(i))) {
      ids.push_back(mark->id());
    }
  }
  store_->Reorder(ids);
}

void PinSync::SaveSession() {
  Profile* profile = window_->GetProfile();
  SessionService* service = SessionServiceFactory::GetForProfile(profile);
  if (!service) {
    return;
  }
  std::map<SessionID, std::string> saved;
  for (int i = 0; i < tabs_->count(); ++i) {
    content::WebContents* page = tabs_->GetWebContentsAt(i);
    const PinMark* mark = MarkOf(page);
    const std::string value =
        mark && tabs_->IsTabPinned(i) ? mark->id() : std::string();
    const SessionID tab_id = sessions::SessionTabHelper::IdForTab(page);
    const auto before = saved_.find(tab_id);
    if (value.empty() && before == saved_.end()) {
      continue;
    }
    if (before == saved_.end() || before->second != value) {
      service->AddTabExtraData(window_->GetSessionID(), tab_id, kSessionKey,
                               value);
    }
    if (!value.empty()) {
      saved[tab_id] = value;
    }
  }
  saved_ = std::move(saved);
}

}  // namespace uw
