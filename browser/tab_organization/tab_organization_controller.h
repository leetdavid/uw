// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#ifndef UW_BROWSER_TAB_ORGANIZATION_TAB_ORGANIZATION_CONTROLLER_H_
#define UW_BROWSER_TAB_ORGANIZATION_TAB_ORGANIZATION_CONTROLLER_H_

#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "base/callback_list.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "chrome/browser/ui/tabs/tab_strip_model_observer.h"
#include "components/tab_groups/tab_group_id.h"
#include "components/tab_groups/tab_group_visual_data.h"
#include "components/tabs/public/tab_interface.h"
#include "ui/base/unowned_user_data/scoped_unowned_user_data.h"
#include "uw/components/tab_organization/page_tree.h"

class BrowserWindowInterface;
class TabStripModel;

namespace content {
class WebContents;
}

namespace ui {
class SimpleMenuModel;
}

namespace uw {

// Owned by EmbedderBrowserWindowFeatures. Mirrors the window's unpinned tabs
// into a PageTree and applies tree changes back through TabStripModel. A task
// is a native tab group; this class never draws one.
class TabOrganizationController : public TabStripModelObserver {
 public:
  DECLARE_USER_DATA(TabOrganizationController);
  static constexpr char kSessionKey[] = "uw.page_tree";
  // Who asked for a move. User moves are reported to ObserveManualMoves.
  enum class Origin { kUser, kOrganizer };
  enum Command {
    kNewTask = 56000,
    kScratchpad,
    kNest,
    kOutdent,
    kCloseBranch,
    kUndo,
    kLastCommand = kUndo,
  };
  static bool IsCommand(int command);
  static void AppendCommands(ui::SimpleMenuModel* menu);

  explicit TabOrganizationController(BrowserWindowInterface* window);
  ~TabOrganizationController() override;

  static TabOrganizationController* From(BrowserWindowInterface* window);
  static TabOrganizationController* From(TabStripModel* model);
  // Called after a tab opens in `window`. `source` is the page whose link
  // opened it, or null for a blank tab.
  static void DidOpenTab(BrowserWindowInterface* window,
                         content::WebContents* page,
                         content::WebContents* source);
  static void RestoreTab(BrowserWindowInterface* window,
                         content::WebContents* page,
                         const std::map<std::string, std::string>& extra_data);
  // Opens a new tab in a new task of its own. Returns false when `window`
  // has no tab tree, in which case nothing opened.
  static bool OpenNewTask(BrowserWindowInterface* window);

  const PageTree& tree() const { return tree_; }
  std::optional<PageId> IdFor(const tabs::TabInterface* tab) const;
  tabs::TabInterface* TabFor(const PageId& id) const;
  // Nesting level under parent pages. 0 for a top-level or untracked tab.
  int DepthFor(const tabs::TabInterface* tab) const;
  // True for a tab opened in this window by a link or a new-tab command, as
  // opposed to one that was restored or dragged in.
  bool WasOpenedHere(const PageId& id) const { return opened_.contains(id); }
  // The selected pages when `context` is selected or null; else `context`.
  std::vector<PageId> SelectionFor(
      const tabs::TabInterface* context = nullptr) const;

  bool CanExecute(int command, const std::vector<PageId>& ids) const;
  void Execute(int command, const std::vector<PageId>& ids);

  // Each call is one undoable organization action over whole branches.
  std::optional<tab_groups::TabGroupId> MoveToNewTask(
      const std::vector<PageId>& ids,
      std::u16string title,
      Origin origin = Origin::kUser);
  bool MoveToTask(const std::vector<PageId>& ids,
                  const tab_groups::TabGroupId& group,
                  Origin origin = Origin::kUser);
  bool MoveToScratchpad(const std::vector<PageId>& ids,
                        Origin origin = Origin::kUser);
  bool Nest(const PageId& id);
  bool Outdent(const PageId& id);
  void CloseBranches(const std::vector<PageId>& ids);
  bool CanUndo() const { return !undo_.empty(); }
  void Undo();

  // A tab drag reorders the strip natively. The tree follows each step, and
  // the strip is put in tree order once the drag ends.
  void BeginTabDrag();
  void EndTabDrag();

  std::optional<tab_groups::TabGroupId> GroupFor(const std::string& task) const;
  std::u16string TaskTitle(const std::string& task) const;
  // Renames a task. It is not an organization action, so undo leaves the
  // name alone.
  void NameTask(const std::string& task, const std::u16string& title);

  base::CallbackListSubscription Observe(base::RepeatingClosure callback);
  // Runs with the pages the user moved by a command, a drag, a native group
  // change, or an undo. Each moved branch is listed in full.
  using ManualMoveCallback =
      base::RepeatingCallback<void(const std::vector<PageId>&)>;
  base::CallbackListSubscription ObserveManualMoves(
      ManualMoveCallback callback);

 private:
  struct Snapshot {
    PageTree tree;
    std::map<std::string, tab_groups::TabGroupVisualData> tasks;
  };
  struct PendingTask {
    std::u16string title;
    std::optional<tab_groups::TabGroupVisualData> visuals;
  };

  void OnTabStripModelChanged(
      TabStripModel* model,
      const TabStripModelChange& change,
      const TabStripSelectionChange& selection) override;
  void OnTabPinnedStateChanged(tabs::TabInterface* tab, int index) override;
  void TabGroupedStateChanged(TabStripModel* model,
                              std::optional<tab_groups::TabGroupId> old_group,
                              std::optional<tab_groups::TabGroupId> new_group,
                              tabs::TabInterface* tab,
                              int index) override;
  void OnSplitTabChanged(const SplitTabChange& change) override;

  // Branch roots of `ids`: pages with no selected ancestor, in tree order.
  std::vector<PageId> Roots(const std::vector<PageId>& ids) const;
  void NotifyManualMove(const std::vector<PageId>& roots);
  void NotifyNativeMoves(const std::set<PageId>& moved);
  // Brings `tree_` up to date with the strip. Never changes the strip.
  void Mirror(const std::set<PageId>& moved);
  void LinkRestoredPages();
  // Makes the strip match `tree_`: group membership first, then order.
  void Apply();
  void ApplyTasks();
  void ApplyOrder();
  void ScheduleApply();
  void ApplyWhenIdle();
  void Record();
  void ScheduleUpdate();
  void Update();

  const raw_ptr<BrowserWindowInterface> window_;
  const raw_ptr<TabStripModel> tabs_;
  PageTree tree_;
  std::map<tabs::TabHandle, PageId> ids_;
  std::optional<PageId> active_;
  std::optional<PageId> previous_active_;
  // Parent links from session restore or another window, kept until both
  // pages are in this window.
  std::map<PageId, PageId> restored_parents_;
  std::map<PageId, PageId> saved_parents_;
  // Pages first seen since the last update. Chromium groups a new tab as it
  // inserts it, which is not a move the user made.
  std::set<PageId> fresh_;
  std::set<PageId> opened_;
  std::map<std::string, PendingTask> pending_tasks_;
  std::vector<Snapshot> undo_;
  bool applying_ = false;
  bool apply_pending_ = false;
  bool dragging_ = false;
  bool update_pending_ = false;
  base::RepeatingClosureList changed_;
  base::RepeatingCallbackList<void(const std::vector<PageId>&)> manual_moves_;
  ui::ScopedUnownedUserData<TabOrganizationController> user_data_;
  base::WeakPtrFactory<TabOrganizationController> weak_factory_{this};
};

}  // namespace uw

#endif  // UW_BROWSER_TAB_ORGANIZATION_TAB_ORGANIZATION_CONTROLLER_H_
