// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/browser/pins/pin_sync.h"

#include <algorithm>
#include <utility>
#include <vector>

#include "base/auto_reset.h"
#include "base/containers/adapters.h"
#include "base/functional/bind.h"
#include "base/task/sequenced_task_runner.h"
#include "base/time/time.h"
#include "chrome/browser/lifetime/browser_shutdown.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/sessions/session_service.h"
#include "chrome/browser/sessions/session_service_factory.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface_iterator.h"
#include "chrome/browser/ui/navigator/browser_navigator.h"
#include "chrome/browser/ui/navigator/browser_navigator_params.h"
#include "chrome/browser/ui/tabs/tab_enums.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "components/sessions/content/session_tab_helper.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/reload_type.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_observer.h"
#include "content/public/browser/web_contents_user_data.h"
#include "content/public/common/referrer.h"
#include "ui/base/page_transition_types.h"
#include "ui/base/window_open_disposition.h"
#include "ui/gfx/image/image_skia.h"
#include "uw/browser/pins/pin_pages.h"
#include "uw/browser/tab_organization/page_copy.h"
#include "uw/browser/workspaces/workspace_window.h"

namespace uw {
namespace {

// What a navigation that started in a placeholder asked for, to do in the
// live page instead.
struct Redo {
  bool reload = false;
  int offset = 0;
  GURL url;
  ui::PageTransition transition = ui::PAGE_TRANSITION_LINK;
};

void RedoInLivePage(base::WeakPtr<content::WebContents> page, Redo redo);

// The pin a tab shows. It stays with the page when the page moves to another
// window. `restored` is set for a mark read back from the session, until the
// window next applies the store, and `placeholder` for a page that only keeps
// the history of the pin's live page, which is elsewhere.
class PinMark : public content::WebContentsUserData<PinMark>,
                public content::WebContentsObserver {
 public:
  ~PinMark() override = default;

  const std::string& id() const { return id_; }
  bool restored() const { return restored_; }
  bool placeholder() const { return placeholder_; }

  // content::WebContentsObserver:
  void DidStartNavigation(content::NavigationHandle* handle) override {
    if (!placeholder_ || !handle->IsInPrimaryMainFrame() ||
        handle->IsSameDocument()) {
      return;
    }
    Redo redo;
    redo.reload = handle->GetReloadType() != content::ReloadType::NONE;
    redo.offset = handle->GetNavigationEntryOffset();
    redo.url = handle->GetURL();
    redo.transition = handle->GetPageTransition();
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE,
        base::BindOnce(&RedoInLivePage, web_contents()->GetWeakPtr(), redo));
  }

 private:
  friend WebContentsUserData;
  PinMark(content::WebContents* page,
          std::string id,
          bool restored,
          bool placeholder)
      : WebContentsUserData(*page),
        content::WebContentsObserver(page),
        id_(std::move(id)),
        restored_(restored),
        placeholder_(placeholder) {}

  const std::string id_;
  const bool restored_;
  const bool placeholder_;
  WEB_CONTENTS_USER_DATA_KEY_DECL();
};

WEB_CONTENTS_USER_DATA_KEY_IMPL(PinMark);

const PinMark* MarkOf(content::WebContents* page) {
  return page ? PinMark::FromWebContents(page) : nullptr;
}

void Mark(content::WebContents* page,
          std::string id,
          bool restored = false,
          bool placeholder = false) {
  page->RemoveUserData(PinMark::UserDataKey());
  PinMark::CreateForWebContents(page, std::move(id), restored, placeholder);
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
  tab->GetContents()->GetController().LoadURL(url, content::Referrer(),
                                              ui::PAGE_TRANSITION_AUTO_BOOKMARK,
                                              std::string());
}

// A page to stand in for `page`. It stays unloaded when shown, as a discarded
// tab does.
std::unique_ptr<content::WebContents> MakePlaceholder(
    content::WebContents* page,
    const std::string& id) {
  std::unique_ptr<content::WebContents> placeholder =
      CopyHistory(page, /*needs_reload=*/false);
  Mark(placeholder.get(), id, /*restored=*/false, /*placeholder=*/true);
  return placeholder;
}

// Makes a placeholder the live page, loading it from its history.
void Wake(content::WebContents* page, std::string id) {
  Mark(page, std::move(id));
  page->GetController().SetNeedsReload();
  page->GetController().LoadIfNecessary();
}

// Keeps a picture of `page` as it looks now, for the hazed view other windows
// show once it leaves.
void KeepPicture(content::WebContents* page, const std::string& id) {
  PinPages* pages =
      PinPages::For(Profile::FromBrowserContext(page->GetBrowserContext()));
  if (!pages) {
    return;
  }
  TakePicture(page, base::BindOnce(
                        [](base::WeakPtr<PinPages> pages, std::string id,
                           gfx::ImageSkia picture) {
                          if (pages) {
                            pages->SetPicture(std::move(id),
                                              std::move(picture));
                          }
                        },
                        pages->GetWeakPtr(), id));
}

void RedoInLivePage(base::WeakPtr<content::WebContents> page, Redo redo) {
  tabs::TabInterface* tab =
      page ? tabs::TabInterface::MaybeGetFromContents(page.get()) : nullptr;
  const PinMark* mark = tab ? MarkOf(tab->GetContents()) : nullptr;
  if (!mark || !mark->placeholder()) {
    return;
  }
  page->Stop();
  PinSync::BringLivePage(tab);
  content::WebContents* live = tab->GetContents();
  if (live == page.get() || !MarkOf(live) || MarkOf(live)->placeholder()) {
    return;
  }
  content::NavigationController& controller = live->GetController();
  if (redo.reload) {
    controller.Reload(content::ReloadType::NORMAL, /*check_for_repost=*/true);
  } else if (redo.offset) {
    if (controller.CanGoToOffset(redo.offset)) {
      controller.GoToOffset(redo.offset);
    }
  } else if (redo.url.is_valid()) {
    controller.LoadURL(redo.url, content::Referrer(), redo.transition,
                       std::string());
  }
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
  reveal_subscription_ = WorkspaceWindow::ObserveReveals(
      base::BindRepeating(&PinSync::OnRevealed, base::Unretained(this)));
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
content::WebContents* PinSync::RestoreTab(
    BrowserWindowInterface* window,
    content::WebContents* page,
    const std::map<std::string, std::string>& extra_data) {
  const auto saved = extra_data.find(kSessionKey);
  if (!page || saved == extra_data.end() || saved->second.empty()) {
    return page;
  }
  const std::string id = saved->second;
  Mark(page, id, /*restored=*/true);
  PinSync* sync = From(window);
  const tabs::TabInterface* tab =
      tabs::TabInterface::MaybeGetFromContents(page);
  if (!sync || !sync->store_ || !tab || !tab->IsPinned()) {
    return page;
  }
  const auto placeholder = extra_data.find(kPlaceholderKey);
  if ((placeholder == extra_data.end() || placeholder->second.empty()) &&
      !sync->LivePageIsElsewhere(id)) {
    return page;
  }
  std::unique_ptr<content::WebContents> made = MakePlaceholder(page, id);
  Mark(made.get(), id, /*restored=*/true, /*placeholder=*/true);
  content::WebContents* stand_in = made.get();
  base::AutoReset<bool> applying(&sync->applying_, true);
  sync->tabs_->DiscardWebContents(page, std::move(made));
  return stand_in;
}

// static
void PinSync::AddRestoreData(const tabs::TabInterface* tab,
                             std::map<std::string, std::string>& extra_data) {
  const PinMark* mark =
      tab && tab->IsPinned() ? MarkOf(tab->GetContents()) : nullptr;
  if (!mark) {
    return;
  }
  extra_data[kSessionKey] = mark->id();
  if (mark->placeholder()) {
    extra_data[kPlaceholderKey] = "1";
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
void PinSync::WillCloseWindow(BrowserWindowInterface* window) {
  PinSync* sync = From(window);
  if (sync && sync->store_ && !browser_shutdown::IsTryingToQuit()) {
    sync->HoldLivePages();
  }
}

// static
bool PinSync::IsPlaceholder(const tabs::TabInterface* tab) {
  const PinMark* mark = tab ? MarkOf(tab->GetContents()) : nullptr;
  return mark && mark->placeholder() && PinFor(tab);
}

// static
void PinSync::BringLivePage(tabs::TabInterface* tab) {
  PinSync* sync = tab ? From(tab->GetBrowserWindowInterface()) : nullptr;
  if (sync && sync->store_) {
    sync->BringInto(sync->tabs_->GetIndexOfTab(tab));
  }
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
  if (selection.active_tab_changed() &&
      selection.reason != TabStripModelObserver::CHANGE_REASON_REPLACED) {
    if (const PinMark* mark = MarkOf(selection.new_contents);
        mark && mark->placeholder()) {
      ScheduleBring(mark->id());
    }
  }
  switch (change.type()) {
    case TabStripModelChange::kInserted:
      if (std::ranges::any_of(
              change.GetInsert()->contents,
              [](const auto& added) { return added.tab->IsPinned(); })) {
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
      // Chromium discarded the tab: the page that replaces it shows the same
      // pin.
      const TabStripModelChange::Replace* replace = change.GetReplace();
      const PinMark* mark = MarkOf(replace->old_contents);
      if (mark && !MarkOf(replace->new_contents)) {
        Mark(replace->new_contents, mark->id(), /*restored=*/false,
             mark->placeholder());
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
    const std::string id = mark->id();
    if (mark->placeholder()) {
      // The live page stays a regular tab where it is. When no window holds
      // it, it opens here.
      base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
          FROM_HERE,
          base::BindOnce(&PinSync::UnpinPlaceholder, weak_factory_.GetWeakPtr(),
                         tab->GetWeakPtr(), id,
                         WindowWithLivePage(id) != nullptr));
    } else {
      Unmark(page);
    }
    store_->Remove(id);
  }
  SaveSession();
}

void PinSync::OnTabChangedAt(tabs::TabInterface* tab,
                             TabChangeType change_type) {
  const PinMark* mark = MarkOf(tab->GetContents());
  if (!applying_ && mark && !mark->placeholder() && tab->IsPinned()) {
    ScheduleRefresh(mark->id());
  }
}

void PinSync::ScheduleApply() {
  if (apply_pending_) {
    return;
  }
  apply_pending_ = true;
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(&PinSync::Apply, weak_factory_.GetWeakPtr()));
}

void PinSync::ScheduleBring(const std::string& id) {
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE,
      base::BindOnce(&PinSync::Bring, weak_factory_.GetWeakPtr(), id));
}

void PinSync::Apply() {
  apply_pending_ = false;
  if (tabs_->closing_all() || tabs_->empty()) {
    return;
  }
  base::AutoReset<bool> applying(&applying_, true);
  PinPages* pages = PinPages::For(window_->GetProfile());

  // Keep one copy of each pin: the live page first, then a restored one, then
  // the selected one.
  const auto rank = [&](int i) {
    const PinMark* mark = MarkOf(tabs_->GetWebContentsAt(i));
    return (mark->placeholder() ? 0 : 4) + (mark->restored() ? 2 : 0) +
           (i == tabs_->active_index() ? 1 : 0);
  };
  std::map<std::string, int> keep;
  for (int i = 0; i < tabs_->IndexOfFirstNonPinnedTab(); ++i) {
    const PinMark* mark = MarkOf(tabs_->GetWebContentsAt(i));
    if (!mark) {
      continue;
    }
    const auto [kept, added] = keep.try_emplace(mark->id(), i);
    if (!added && rank(i) > rank(kept->second)) {
      kept->second = i;
    }
  }
  // A copy of a pin another window unpinned goes, unless it is the live page,
  // which stays as a regular tab. A restored pin the store has lost is
  // adopted below.
  const auto unpinned = [&](const PinMark* mark) {
    return !store_->Find(mark->id()) && !mark->restored();
  };
  std::vector<int> closing;
  for (int i = 0; i < tabs_->IndexOfFirstNonPinnedTab(); ++i) {
    const PinMark* mark = MarkOf(tabs_->GetWebContentsAt(i));
    if (mark &&
        (keep[mark->id()] != i || (unpinned(mark) && mark->placeholder()))) {
      closing.push_back(i);
    }
  }
  for (int index : base::Reversed(closing)) {
    tabs_->CloseWebContentsAt(index, TabCloseTypes::CLOSE_NONE);
  }
  for (int i = tabs_->IndexOfFirstNonPinnedTab() - 1; i >= 0; --i) {
    content::WebContents* page = tabs_->GetWebContentsAt(i);
    if (const PinMark* mark = MarkOf(page); mark && unpinned(mark)) {
      Unmark(page);
      tabs_->SetTabPinned(i, false);
    }
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
    const std::string& id = pins[index].id;
    int at = -1;
    for (int i = 0; i < tabs_->IndexOfFirstNonPinnedTab(); ++i) {
      const PinMark* mark = MarkOf(tabs_->GetWebContentsAt(i));
      if (mark && mark->id() == id) {
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
    // A live page no window holds comes here as it was. While another window
    // holds it, this window gets a placeholder.
    if (std::unique_ptr<content::WebContents> page = pages->Take(id)) {
      tabs_->InsertWebContentsAt(index, std::move(page),
                                 AddTabTypes::ADD_PINNED);
      continue;
    }
    if (content::WebContents* live = FindLivePage(id)) {
      tabs_->InsertWebContentsAt(index, MakePlaceholder(live, id),
                                 AddTabTypes::ADD_PINNED);
      continue;
    }
    NavigateParams params(window_, pins[index].home,
                          ui::PAGE_TRANSITION_AUTO_BOOKMARK);
    params.disposition = WindowOpenDisposition::NEW_BACKGROUND_TAB;
    params.tabstrip_index = index;
    params.tabstrip_add_types = AddTabTypes::ADD_PINNED;
    Navigate(&params);
    if (params.navigated_or_inserted_contents) {
      Mark(params.navigated_or_inserted_contents, id);
    }
  }

  // One live page per pin: a kept page is the pin as it was, and takes the
  // place of a copy here, and a copy here gives way to a page another window
  // holds.
  for (int i = 0; i < tabs_->IndexOfFirstNonPinnedTab(); ++i) {
    content::WebContents* page = tabs_->GetWebContentsAt(i);
    const PinMark* mark = MarkOf(page);
    if (!mark || mark->placeholder()) {
      continue;
    }
    const std::string id = mark->id();
    if (std::unique_ptr<content::WebContents> kept = pages->Take(id)) {
      tabs_->DiscardWebContents(page, std::move(kept));
    } else if (LivePageIsElsewhere(id)) {
      tabs_->DiscardWebContents(page, MakePlaceholder(page, id));
    }
  }
  // A window that Cmd+Shift+T brings back takes the page it gave up.
  for (int i = 0; i < tabs_->IndexOfFirstNonPinnedTab(); ++i) {
    content::WebContents* page = tabs_->GetWebContentsAt(i);
    const PinMark* mark = MarkOf(page);
    if (mark && mark->placeholder() && mark->restored()) {
      if (std::unique_ptr<content::WebContents> kept =
              pages->Take(mark->id())) {
        tabs_->DiscardWebContents(page, std::move(kept));
      }
    }
  }

  // From now on a restored pin that leaves the store was unpinned elsewhere,
  // so its copy here goes instead of coming back as a new pin.
  for (int i = 0; i < tabs_->IndexOfFirstNonPinnedTab(); ++i) {
    content::WebContents* page = tabs_->GetWebContentsAt(i);
    if (const PinMark* mark = MarkOf(page); mark && mark->restored()) {
      Mark(page, mark->id(), /*restored=*/false, mark->placeholder());
    }
  }
  SaveSession();
}

void PinSync::Bring(const std::string& id) {
  const int at = tabs_->active_index();
  const PinMark* mark = at >= 0 ? MarkOf(tabs_->GetWebContentsAt(at)) : nullptr;
  if (mark && mark->id() == id) {
    BringInto(at);
  }
}

bool PinSync::BringInto(int index) {
  if (index < 0 || index >= tabs_->IndexOfFirstNonPinnedTab() ||
      tabs_->closing_all() || WorkspaceWindow::IsHidden(window_)) {
    return false;
  }
  content::WebContents* here = tabs_->GetWebContentsAt(index);
  const PinMark* mark = MarkOf(here);
  if (!mark || !mark->placeholder()) {
    return false;
  }
  const std::string id = mark->id();
  std::unique_ptr<content::WebContents> page = TakeLivePage(id);
  base::AutoReset<bool> applying(&applying_, true);
  if (page) {
    tabs_->DiscardWebContents(here, std::move(page));
  } else {
    Wake(here, id);
  }
  SaveSession();
  return true;
}

void PinSync::OnRevealed(BrowserWindowInterface* window) {
  if (window == window_) {
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(&PinSync::FollowToThisWindow,
                                  weak_factory_.GetWeakPtr()));
  }
}

void PinSync::FollowToThisWindow() {
  const int at = tabs_->active_index();
  const PinMark* mark = at >= 0 ? MarkOf(tabs_->GetWebContentsAt(at)) : nullptr;
  if (!mark || !mark->placeholder()) {
    return;
  }
  const PinSync* holder = WindowWithLivePage(mark->id());
  if (!holder || WorkspaceWindow::IsHidden(holder->window_)) {
    BringInto(at);
  }
}

void PinSync::ScheduleRefresh(const std::string& id) {
  if (!refreshing_.insert(id).second) {
    return;
  }
  base::SequencedTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE,
      base::BindOnce(&PinSync::RefreshPlaceholders, weak_factory_.GetWeakPtr(),
                     id),
      base::Milliseconds(300));
}

void PinSync::RefreshPlaceholders(const std::string& id) {
  refreshing_.erase(id);
  const int at = IndexOfLivePage(id);
  if (at < 0) {
    return;
  }
  content::WebContents* live = tabs_->GetWebContentsAt(at);
  for (PinSync* sync : Others()) {
    for (int i = 0; i < sync->tabs_->IndexOfFirstNonPinnedTab(); ++i) {
      content::WebContents* page = sync->tabs_->GetWebContentsAt(i);
      const PinMark* mark = MarkOf(page);
      if (!mark || !mark->placeholder() || mark->id() != id ||
          sync->tabs_->closing_all() || SameHistory(page, live)) {
        continue;
      }
      base::AutoReset<bool> applying(&sync->applying_, true);
      sync->tabs_->DiscardWebContents(page, MakePlaceholder(live, id));
      sync->SaveSession();
    }
  }
}

void PinSync::UnpinPlaceholder(base::WeakPtr<tabs::TabInterface> tab,
                               const std::string& id,
                               bool elsewhere) {
  const int index =
      tab ? tabs_->GetIndexOfTab(tab.get()) : TabStripModel::kNoTab;
  if (index == TabStripModel::kNoTab) {
    return;
  }
  content::WebContents* here = tab->GetContents();
  base::AutoReset<bool> applying(&applying_, true);
  if (elsewhere) {
    tabs_->CloseWebContentsAt(index, TabCloseTypes::CLOSE_NONE);
    return;
  }
  if (std::unique_ptr<content::WebContents> page =
          PinPages::For(window_->GetProfile())->Take(id)) {
    Unmark(page.get());
    tabs_->DiscardWebContents(here, std::move(page));
  } else {
    Unmark(here);
    here->GetController().SetNeedsReload();
    here->GetController().LoadIfNecessary();
  }
  SaveSession();
}

std::unique_ptr<content::WebContents> PinSync::TakeLivePage(
    const std::string& id) {
  if (std::unique_ptr<content::WebContents> page =
          PinPages::For(window_->GetProfile())->Take(id)) {
    return page;
  }
  for (PinSync* sync : Others()) {
    const int at = sync->IndexOfLivePage(id);
    if (at < 0 || sync->tabs_->closing_all()) {
      continue;
    }
    content::WebContents* page = sync->tabs_->GetWebContentsAt(at);
    KeepPicture(page, id);
    base::AutoReset<bool> applying(&sync->applying_, true);
    std::unique_ptr<content::WebContents> taken =
        sync->tabs_->DiscardWebContents(page, MakePlaceholder(page, id));
    sync->SaveSession();
    return taken;
  }
  return nullptr;
}

std::vector<PinSync*> PinSync::Others() const {
  std::vector<PinSync*> others;
  for (BrowserWindowInterface* other : GetAllBrowserWindowInterfaces()) {
    PinSync* sync = From(other);
    if (sync && sync != this && sync->store_ == store_) {
      others.push_back(sync);
    }
  }
  return others;
}

int PinSync::IndexOfLivePage(std::string_view id) const {
  for (int i = 0; i < tabs_->IndexOfFirstNonPinnedTab(); ++i) {
    const PinMark* mark = MarkOf(tabs_->GetWebContentsAt(i));
    if (mark && mark->id() == id && !mark->placeholder()) {
      return i;
    }
  }
  return -1;
}

PinSync* PinSync::WindowWithLivePage(std::string_view id) const {
  for (PinSync* sync : Others()) {
    if (sync->IndexOfLivePage(id) >= 0) {
      return sync;
    }
  }
  return nullptr;
}

content::WebContents* PinSync::FindLivePage(std::string_view id) const {
  const PinSync* sync = WindowWithLivePage(id);
  return sync ? sync->tabs_->GetWebContentsAt(sync->IndexOfLivePage(id))
              : nullptr;
}

bool PinSync::LivePageIsElsewhere(std::string_view id) const {
  return PinPages::For(window_->GetProfile())->Holds(id) || FindLivePage(id);
}

void PinSync::HoldLivePages() {
  PinPages* pages = PinPages::For(window_->GetProfile());
  base::AutoReset<bool> applying(&applying_, true);
  for (int i = 0; i < tabs_->IndexOfFirstNonPinnedTab(); ++i) {
    content::WebContents* page = tabs_->GetWebContentsAt(i);
    const PinMark* mark = MarkOf(page);
    if (!mark || mark->placeholder() || !store_->Find(mark->id())) {
      continue;
    }
    const std::string id = mark->id();
    KeepPicture(page, id);
    std::unique_ptr<content::WebContents> live =
        tabs_->DiscardWebContents(page, MakePlaceholder(page, id));
    // A page that has not loaded since the restart still waits in Chromium's
    // queue of restored tabs to load, which expects each one in a tab. A copy
    // of its history that was never a tab outlives the window instead.
    if (live->GetController().NeedsReload()) {
      live = CopyHistory(live.get(), /*needs_reload=*/true);
      Mark(live.get(), id);
    }
    pages->Hold(id, std::move(live));
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
    const bool pinned = mark && tabs_->IsTabPinned(i);
    const std::string id = pinned ? mark->id() : std::string();
    const std::string placeholder =
        pinned && mark->placeholder() ? "1" : std::string();
    const std::string value = id.empty() ? id : id + "/" + placeholder;
    const SessionID tab_id = sessions::SessionTabHelper::IdForTab(page);
    const auto before = saved_.find(tab_id);
    if (value.empty() && before == saved_.end()) {
      continue;
    }
    if (before == saved_.end() || before->second != value) {
      service->AddTabExtraData(window_->GetSessionID(), tab_id, kSessionKey,
                               id);
      service->AddTabExtraData(window_->GetSessionID(), tab_id, kPlaceholderKey,
                               placeholder);
    }
    if (!value.empty()) {
      saved[tab_id] = value;
    }
  }
  saved_ = std::move(saved);
}

}  // namespace uw
