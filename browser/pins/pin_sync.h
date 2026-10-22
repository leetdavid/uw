// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#ifndef UW_BROWSER_PINS_PIN_SYNC_H_
#define UW_BROWSER_PINS_PIN_SYNC_H_

#include <map>
#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "base/callback_list.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "chrome/browser/ui/tabs/tab_strip_model_observer.h"
#include "components/sessions/core/session_id.h"
#include "ui/base/unowned_user_data/scoped_unowned_user_data.h"
#include "url/gurl.h"
#include "uw/browser/pins/pin_store.h"

class BrowserWindowInterface;
class TabStripModel;

namespace content {
class WebContents;
}

namespace tabs {
class TabInterface;
}

namespace uw {

// Owned by EmbedderBrowserWindowFeatures. Keeps a window's pinned tabs the
// same as its profile's PinStore: a tab pinned, unpinned, or moved among the
// pins here changes the store, and a change to the store from another window
// is applied here. Each pin has one live page in the profile. The other
// windows' tabs for it hold a placeholder with the page's history and no
// renderer, and selecting one moves the live page into it. So does Reload,
// Back, or any other navigation in a placeholder, which then runs in the live
// page.
class PinSync : public TabStripModelObserver {
 public:
  DECLARE_USER_DATA(PinSync);
  static constexpr char kSessionKey[] = "uw.pin";
  // Set on a pinned tab that held a placeholder.
  static constexpr char kPlaceholderKey[] = "uw.pin_placeholder";

  explicit PinSync(BrowserWindowInterface* window);
  PinSync(const PinSync&) = delete;
  PinSync& operator=(const PinSync&) = delete;
  ~PinSync() override;

  static PinSync* From(BrowserWindowInterface* window);

  // Marks a restored tab of `window` with the pin it showed, from its session
  // data. A copy that held a placeholder, or whose live page another window
  // already holds, becomes a placeholder before it loads. Returns the tab's
  // page.
  static content::WebContents* RestoreTab(
      BrowserWindowInterface* window,
      content::WebContents* page,
      const std::map<std::string, std::string>& extra_data);
  // What Cmd+Shift+T needs to bring `tab` back as the same pin.
  static void AddRestoreData(const tabs::TabInterface* tab,
                             std::map<std::string, std::string>& extra_data);

  // The pin `tab` shows, or null for a tab that shows none.
  static const PinStore::Pin* PinFor(const tabs::TabInterface* tab);
  // Whether Cmd-W leaves `window`'s selected tab open because it is a pin.
  static bool KeepsActiveTab(BrowserWindowInterface* window);
  // Whether a click or a menu row may close `tab`. Pins are only unpinned.
  static bool CanClose(const tabs::TabInterface* tab);

  // Moves the live pages of `window`'s pins out of it before it closes, so
  // they outlive it, unless uw is quitting. See PinPages.
  static void WillCloseWindow(BrowserWindowInterface* window);
  // Whether `tab` holds a placeholder for a pin whose live page is elsewhere.
  static bool IsPlaceholder(const tabs::TabInterface* tab);
  // Moves the live page of the pin whose placeholder `tab` holds into it.
  static void BringLivePage(tabs::TabInterface* tab);

  // Loads the pin's home URL in `tab`.
  static void ResetToHome(const tabs::TabInterface* tab);
  // Makes the page `tab` shows its pin's home URL, without loading anything.
  static void ReplaceHomeWithCurrent(const tabs::TabInterface* tab);
  // Makes `home` the pin's home URL and loads it in `tab`.
  static void SetHome(const tabs::TabInterface* tab, const GURL& home);

  // TabStripModelObserver:
  void OnTabStripModelChanged(
      TabStripModel* model,
      const TabStripModelChange& change,
      const TabStripSelectionChange& selection) override;
  void OnTabPinnedStateChanged(tabs::TabInterface* tab, int index) override;
  void OnTabChangedAt(tabs::TabInterface* tab,
                      TabChangeType change_type) override;

 private:
  void ScheduleApply();
  void ScheduleBring(const std::string& id);
  // Moves pin `id`'s live page into this window's selected tab for it.
  void Bring(const std::string& id);
  // Moves the live page into the placeholder at `index`, or loads the
  // placeholder when no window holds the page. Whether the page is there now.
  bool BringInto(int index);
  // After a workspace switch shows this window: the page of its selected pin
  // follows when no shown window holds it.
  void OnRevealed(BrowserWindowInterface* window);
  void FollowToThisWindow();
  // Gives the placeholders of pin `id` the live page's history, title, and
  // icon, a moment after the page changes.
  void ScheduleRefresh(const std::string& id);
  void RefreshPlaceholders(const std::string& id);
  // Takes pin `id`'s live page from the window that holds it, leaving a
  // placeholder there, or from PinPages.
  std::unique_ptr<content::WebContents> TakeLivePage(const std::string& id);
  // Finishes unpinning a placeholder: it closes while another window holds
  // the live page. Otherwise the live page takes its place, or it loads.
  void UnpinPlaceholder(base::WeakPtr<tabs::TabInterface> tab,
                        const std::string& id,
                        bool elsewhere);
  // The other windows' PinSyncs of this profile.
  std::vector<PinSync*> Others() const;
  // This window's tab holding pin `id`'s live page, or -1.
  int IndexOfLivePage(std::string_view id) const;
  // The window of this profile besides this one holding pin `id`'s live page,
  // or null.
  PinSync* WindowWithLivePage(std::string_view id) const;
  // Pin `id`'s live page in another window of this profile, or null.
  content::WebContents* FindLivePage(std::string_view id) const;
  // Whether another window or PinPages holds pin `id`'s live page.
  bool LivePageIsElsewhere(std::string_view id) const;
  void HoldLivePages();
  // Adopts pinned tabs the store does not know, closes copies of pins it no
  // longer has, and adds and orders the rest to match it.
  void Apply();
  void SaveSession();
  // Gives the store this window's order of the pins.
  void ReportOrder();

  const raw_ptr<BrowserWindowInterface> window_;
  const raw_ptr<TabStripModel> tabs_;
  const raw_ptr<PinStore> store_;
  base::CallbackListSubscription store_subscription_;
  base::CallbackListSubscription reveal_subscription_;
  // Pins whose placeholders wait for a refresh.
  std::set<std::string> refreshing_;
  // Set while this class changes the tab strip, so its own changes are not
  // taken for the user's.
  bool applying_ = false;
  bool apply_pending_ = false;
  // What each tab's session data says, so it is written only on a change.
  std::map<SessionID, std::string> saved_;
  ui::ScopedUnownedUserData<PinSync> user_data_;
  base::WeakPtrFactory<PinSync> weak_factory_{this};
};

}  // namespace uw

#endif  // UW_BROWSER_PINS_PIN_SYNC_H_
