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
#include "uw/browser/tab_organization/hidden_tabs.h"
#include "uw/components/tab_organization/page_tree.h"
#include "uw/components/tab_organization/placement.h"

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
class TabOrganizationController : public TabStripModelObserver,
                                  public HiddenTabs {
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
    kMoveUp,
    kMoveDown,
    kToggleBranch,
    kNewChildTab,
    kKeepHere,
    kLastCommand = kKeepHere,
  };
  static bool IsCommand(int command);

  // Adds the commands to the menu of a tab whose commands act on `ids`, each
  // beside the Chromium items it goes with, per the tab menu of the keyboard
  // and mouse plan.
  void AddCommands(ui::SimpleMenuModel* menu,
                   const std::vector<PageId>& ids) const;

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
  // The pages `command` acts on from the menu of `context`, or from the menu
  // bar when `context` is null. "New child tab" takes one page, not the
  // selection.
  std::vector<PageId> TargetsFor(
      int command,
      const tabs::TabInterface* context = nullptr) const;

  bool CanExecute(int command, const std::vector<PageId>& ids) const;
  void Execute(int command, const std::vector<PageId>& ids);

  // Each call is one undoable organization action over whole branches.
  // `staying` are pages inside those branches that do not go along. Each
  // moves up to where the top page of its branch was, with the tabs under it.
  std::optional<tab_groups::TabGroupId> MoveToNewTask(
      const std::vector<PageId>& ids,
      std::u16string title,
      Origin origin = Origin::kUser,
      const std::vector<PageId>& staying = {});
  bool MoveToTask(const std::vector<PageId>& ids,
                  const tab_groups::TabGroupId& group,
                  Origin origin = Origin::kUser,
                  const std::vector<PageId>& staying = {});
  bool MoveToScratchpad(const std::vector<PageId>& ids,
                        Origin origin = Origin::kUser,
                        const std::vector<PageId>& staying = {});
  // Tidy's moves, in order, as one undoable organization action of uw's. A
  // new task starts the first time its name comes up, after every other
  // task, under that name as one uw gave.
  bool ApplyTidy(const std::vector<TidyMove>& moves);
  bool Nest(const PageId& id);
  bool Outdent(const PageId& id);
  // Open a selected blank tab first under `id`, or below its branch.
  void OpenChild(const PageId& id);
  void OpenBelow(const PageId& id);
  // Swaps a branch with the sibling branch above or below it.
  bool MoveUp(const PageId& id);
  bool MoveDown(const PageId& id);
  void CloseBranches(const std::vector<PageId>& ids);
  // Closes `task` and its tabs. A task that holds every tab leaves a blank
  // tab behind, so the window stays.
  void CloseTask(const tab_groups::TabGroupId& task);
  // The Scratchpad's tabs that Clear closes: all but kept tabs and terminals
  // running a command. Each terminal's shell is asked, so `done` runs later.
  void FindClearable(base::OnceCallback<void(std::vector<PageId>)> done) const;
  // Clear: closes the tabs FindClearable finds that are still in the
  // Scratchpad when the shells answer. Closing every tab of the window
  // leaves a blank tab behind, so the window stays.
  void ClearScratchpad();
  // Opens a selected blank tab at the end of `task`.
  void OpenInTask(const tab_groups::TabGroupId& task);
  // Moves `tab`, just opened, to the end of `task` at its top level, and
  // opens the task if it is collapsed.
  void PlaceInTask(const tabs::TabInterface* tab,
                   const tab_groups::TabGroupId& task);

  // A collapsed branch hides the tabs under its top page. Collapsing is not
  // an organization action, so undo leaves it alone.
  bool IsCollapsed(const PageId& id) const { return collapsed_.contains(id); }
  // True when a collapsed branch hides `id` below its top page.
  bool IsHidden(const PageId& id) const;
  bool IsHidden(const tabs::TabInterface* tab) const override;
  // True when ToggleBranches would collapse the branches of `ids`, false
  // when it would expand them.
  bool CollapsesBranches(const std::vector<PageId>& ids) const;
  // Collapses the branches of `ids` that have tabs under them, or expands
  // them when all are collapsed. The selected tab never hides: the top page
  // of its branch is selected in its place.
  void ToggleBranches(const std::vector<PageId>& ids);
  // A kept tab stays where the user left it: the organizer neither asks about
  // it nor moves it, until the user lets it go or moves it to another task or
  // to the Scratchpad. Keeping a tab is not an organization action, so undo
  // leaves it alone.
  bool IsKept(const PageId& id) const;
  // True when ToggleKept would keep the tabs of `ids`, false when it would
  // let them go.
  bool KeepsTabs(const std::vector<PageId>& ids) const;
  // Keeps the tabs of `ids`, or lets them go when all are kept.
  void ToggleKept(const std::vector<PageId>& ids);
  // A held tab is a child the user nested by hand, with "New child tab" or
  // "Nest under previous tab". The organizer does not ask about it, and it
  // goes wherever its parent goes, until the user moves it or its parent
  // closes.
  bool IsHeld(const PageId& id) const;

  // Selects the tab used last in `task`, or in the Scratchpad when `task` is
  // empty. Returns false when it holds no tab.
  bool SelectTask(const std::string& task);
  bool CanUndo() const { return !undo_.empty(); }
  void Undo();
  // What Undo would reverse: the title of the first tab the action moved, and
  // the task it went to, or the Scratchpad. `place` is empty for a move
  // within the tree, such as nesting a tab.
  struct UndoMove {
    std::u16string tab;
    std::u16string place;
  };
  std::optional<UndoMove> NextUndo() const;

  // A tab drag reorders the strip natively. The tree follows each step, and
  // the strip is put in tree order once the drag ends.
  void BeginTabDrag();
  void EndTabDrag();

  std::optional<tab_groups::TabGroupId> GroupFor(const std::string& task) const;
  std::u16string TaskTitle(const std::string& task) const;
  // Gives a task a name of uw's own. It is not an organization action, so
  // undo leaves the name alone.
  void NameTask(const std::string& task, const std::u16string& title);
  // True while `task` has the name uw gave it, in this session or an earlier
  // one. Any other name is the user's.
  bool HasGeneratedName(const std::string& task) const;
  // True while `task` has a name the user typed, which uw never replaces. A
  // task that still has the name every new task starts with has neither.
  bool HasManualName(const std::string& task) const;
  // Hands the name of `task` back to uw. The task keeps the name it has, as
  // one uw gave: uw replaces it once the task has outgrown it.
  void NameAutomatically(const std::string& task);

  // Asks the row of `task` in the sidebar to turn its name into a field, now
  // or once the row is shown. Rows that show tasks observe the requests and
  // take the one for their task.
  void RenameOnItsRow(const std::string& task);
  bool TakeRename(const std::string& task);
  base::CallbackListSubscription ObserveRenames(
      base::RepeatingClosure callback);

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
    std::u16string tab;
    // The task the action moved tabs into; empty for the Scratchpad.
    std::optional<std::string> task;
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
  void OnTabGroupChanged(const TabGroupChange& change) override;
  void TabGroupedStateChanged(TabStripModel* model,
                              std::optional<tab_groups::TabGroupId> old_group,
                              std::optional<tab_groups::TabGroupId> new_group,
                              tabs::TabInterface* tab,
                              int index) override;
  void OnSplitTabChanged(const SplitTabChange& change) override;

  // Branch roots of `ids`: pages with no selected ancestor, in tree order.
  std::vector<PageId> Roots(const std::vector<PageId>& ids) const;
  // Roots of `ids` with tabs under them.
  std::vector<PageId> Parents(const std::vector<PageId>& ids) const;
  // Closes the tabs of `ids` as Chromium's Close Tabs does.
  void Close(std::vector<PageId> ids);
  // Expands every collapsed branch that holds the selected tab below its top
  // page.
  void RevealSelected();
  // Opens a selected blank tab after `anchor` in the strip and returns it.
  std::optional<PageId> OpenBlankTab(const PageId& anchor);
  // Takes each of `staying` out of the branches of `roots`.
  void LeaveBehind(const std::vector<PageId>& roots,
                   const std::vector<PageId>& staying);
  void NotifyManualMove(const std::vector<PageId>& roots);
  // Puts `id` first among the recently selected pages.
  void Remember(const std::optional<PageId>& id);
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
  // Saves the window for Undo before an action that moves `roots`, into
  // `task` when the action changes their task.
  void Record(const std::vector<PageId>& roots,
              std::optional<std::string> task = std::nullopt);
  void ScheduleUpdate();
  void Update();

  const raw_ptr<BrowserWindowInterface> window_;
  const raw_ptr<TabStripModel> tabs_;
  PageTree tree_;
  std::map<tabs::TabHandle, PageId> ids_;
  std::optional<PageId> active_;
  std::optional<PageId> previous_active_;
  // Pages in the order they were last selected, most recent first.
  std::vector<PageId> recent_;
  // Parent links from session restore or another window, kept until both
  // pages are in this window.
  std::map<PageId, PageId> restored_parents_;
  // What the session last saved for each page.
  std::map<PageId, std::string> saved_states_;
  // The name uw gave each task. It is saved with every page of the task, so
  // it comes back with the session.
  std::map<std::string, std::u16string> generated_names_;
  // Pages whose branch is collapsed. Saved with the session.
  std::set<PageId> collapsed_;
  // Kept tabs, each with the task it was kept in. Saved with the session.
  std::map<PageId, std::string> kept_;
  // Held tabs, each with the parent it was nested under. Saved with the
  // session.
  std::map<PageId, PageId> held_;
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
  // The task whose row should turn its name into a field.
  std::string rename_;
  base::RepeatingClosureList renames_;
  base::RepeatingCallbackList<void(const std::vector<PageId>&)> manual_moves_;
  ui::ScopedUnownedUserData<TabOrganizationController> user_data_;
  base::WeakPtrFactory<TabOrganizationController> weak_factory_{this};
};

}  // namespace uw

#endif  // UW_BROWSER_TAB_ORGANIZATION_TAB_ORGANIZATION_CONTROLLER_H_
