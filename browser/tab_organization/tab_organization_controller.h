// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#ifndef UW_BROWSER_TAB_ORGANIZATION_TAB_ORGANIZATION_CONTROLLER_H_
#define UW_BROWSER_TAB_ORGANIZATION_TAB_ORGANIZATION_CONTROLLER_H_

#include <map>
#include <optional>
#include <string>
#include <vector>

#include "base/callback_list.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/timer/timer.h"
#include "chrome/browser/ui/tabs/tab_strip_model_observer.h"
#include "components/prefs/pref_change_registrar.h"
#include "components/tabs/public/tab_interface.h"
#include "ui/base/unowned_user_data/scoped_unowned_user_data.h"
#include "uw/browser/tab_organization/ai_connection.h"
#include "uw/browser/tab_organization/remote_model_client.h"
#include "uw/components/tab_organization/organization_coordinator.h"
#include "uw/components/tab_organization/tab_tree.h"

class BrowserWindowInterface;
class GURL;
class Profile;
class TabStripModel;
namespace ui {
class SimpleMenuModel;
}

namespace content {
class WebContents;
}

namespace uw {

// Owned by EmbedderBrowserWindowFeatures. Views never own page/tree lifetime.
class TabOrganizationController : public TabStripModelObserver {
 public:
  DECLARE_USER_DATA(TabOrganizationController);
  static constexpr char kSessionKey[] = "uw.tab_tree";
  enum Command {
    kNewTask = 56000,
    kCloseBranch,
    kUndo,
    kScratchpad,
    kMoveUp,
    kMoveDown,
    kNest,
    kOutdent,
    kToggleCollapsed,
    kRenameTask,
    kLastCommand = kRenameTask,
  };
  static bool IsCommand(int command);
  static void AppendCommands(ui::SimpleMenuModel* menu);
  explicit TabOrganizationController(BrowserWindowInterface* window);
  ~TabOrganizationController() override;

  static TabOrganizationController* From(BrowserWindowInterface* window);
  static TabOrganizationController* From(TabStripModel* model);
  static void DidOpenLink(BrowserWindowInterface* window,
                          content::WebContents* page,
                          content::WebContents* source);
  static void RestoreTab(BrowserWindowInterface* window,
                         content::WebContents* page,
                         const std::map<std::string, std::string>& extra_data);

  TabTree& tree() { return tree_; }
  const TabTree& tree() const { return tree_; }
  Profile* profile() const;
  std::optional<TreeId> IdFor(const tabs::TabInterface* tab) const;
  tabs::TabInterface* TabFor(const TreeId& id) const;
  std::vector<TreeId> SelectionFor(
      const tabs::TabInterface* context = nullptr) const;
  std::optional<TreeId> MakeTask(const std::vector<TreeId>& ids,
                                 std::u16string title);
  bool Move(const std::vector<TreeId>& ids,
            const TreeId& parent,
            std::optional<TreeId> before = std::nullopt);
  void Rename(const TreeId& id, std::u16string title);
  void CloseBranch(const std::vector<TreeId>& ids);
  void Undo();
  bool CanExecute(int command, const std::vector<TreeId>& ids) const;
  std::optional<TreeId> Execute(int command, const std::vector<TreeId>& ids);

  // The native tab drag retains page ownership and detach/window behavior.
  // Only its final same-window tree destination is handled here.
  void BeginTabDrag();
  void SetDropDestination(TreeId parent, std::optional<TreeId> before);
  void EndTabDrag(bool completed);
  base::WeakPtr<TabOrganizationController> GetWeakPtr();
  base::CallbackListSubscription Observe(base::RepeatingClosure callback);

  // Takes a pending automatic-organization notice for the sidebar, if any.
  struct Notice {
    std::u16string text;
    // True when Undo reverses the action the notice reports.
    bool undoable = false;
  };
  std::optional<Notice> TakeNotice();

  // Validates the configured connection with a live probe request. Enables
  // organization only when it succeeds; otherwise leaves it disabled with a
  // notice.
  void ValidateProviders();

  // Global automatic-organization pause per the product pause choices.
  void SetPause(PauseMode mode, base::TimeDelta duration = base::TimeDelta());
  PauseMode pause_mode() const;

  // Fires the classification pass immediately, bypassing the debounce timer.
  void ClassifyNowForTesting();

 private:
  void OnTabStripModelChanged(
      TabStripModel* model,
      const TabStripModelChange& change,
      const TabStripSelectionChange& selection) override;
  void OnTabChangedAt(tabs::TabInterface* tab, TabChangeType type) override;
  void OnTabPinnedStateChanged(tabs::TabInterface* tab, int index) override;
  void TabGroupedStateChanged(TabStripModel* model,
                              std::optional<tab_groups::TabGroupId> old_group,
                              std::optional<tab_groups::TabGroupId> new_group,
                              tabs::TabInterface* tab,
                              int index) override;
  void OnSplitTabChanged(const SplitTabChange& change) override;

  bool Eligible(tabs::TabInterface* tab) const;
  void Sync();
  void AlignTabOrder();
  void ScheduleUpdate();
  void Update();
  void FinishTabDrag(bool completed);
  struct MoveTarget {
    TreeId parent;
    std::optional<TreeId> before;
  };
  std::optional<MoveTarget> GetMoveTarget(int command, const TreeId& id) const;
  std::vector<tabs::TabInterface*> ResolvePages(const std::vector<TreeId>& ids);

  // Automatic organization: debounced background classification.
  void OnConfigChanged();
  bool AutomaticEnabled();
  void TrackActivityAndSchedule();
  void MaybeScheduleAutomatic();
  void OnClassifyTimer();
  void OnClassificationResponse(uint64_t request_id,
                                RemoteModelClient::Outcome outcome);
  void OnValidationResponse(RemoteModelClient::Outcome outcome);
  void CancelAutomatic();
  std::string BranchSourceDescription(const TreeId& page) const;
  std::string ManualPlacementDescription(const TreeId& page) const;
  std::vector<std::pair<std::string, std::string>> TaskOptions() const;
  void RequestExcerpt(tabs::TabInterface* tab,
                      const TreeId& id,
                      uint64_t revision);
  void StoreExcerpt(TreeId id, uint64_t revision, std::string text);

  const raw_ptr<BrowserWindowInterface> window_;
  const raw_ptr<TabStripModel> tabs_;
  TabTree tree_;
  OrganizationCoordinator coordinator_;
  std::map<tabs::TabHandle, TreeId> pages_;
  // Previously active tab. A lone sourceless new tab nests under it.
  std::optional<TreeId> last_active_;
  std::vector<TreeId> dragged_;
  std::optional<TreeId> drop_parent_;
  std::optional<TreeId> drop_before_;
  bool aligning_ = false;
  bool update_pending_ = false;
  base::CallbackListSubscription tree_subscription_;
  base::RepeatingClosureList changed_;
  ui::ScopedUnownedUserData<TabOrganizationController> user_data_;
  // Automatic classification state. Activity revisions bump on title/URL
  // changes; a newer revision supersedes older pending requests.
  base::OneShotTimer classify_timer_;
  std::map<TreeId, uint64_t> activity_;
  struct PageSnapshot {
    std::u16string title;
    std::string url;
  };
  std::map<TreeId, PageSnapshot> snapshots_;
  struct Excerpt {
    std::string text;
    uint64_t revision = 0;
  };
  // Readable page text captured for the current activity revision.
  std::map<TreeId, Excerpt> excerpts_;
  std::map<TreeId, uint64_t> dispatched_activity_;
  std::map<uint64_t, std::unique_ptr<RemoteModelClient>> pending_;
  std::unique_ptr<RemoteModelClient> probe_;
  PrefChangeRegistrar pref_registrar_;
  std::optional<Notice> notice_;
  base::WeakPtrFactory<TabOrganizationController> weak_factory_{this};
};

}  // namespace uw

#endif  // UW_BROWSER_TAB_ORGANIZATION_TAB_ORGANIZATION_CONTROLLER_H_
