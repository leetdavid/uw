// Copyright 2026 The uw Authors. All rights reserved.

#ifndef UW_BROWSER_TAB_ORGANIZATION_SHARED_TREE_H_
#define UW_BROWSER_TAB_ORGANIZATION_SHARED_TREE_H_

#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "base/feature_list.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/ref_counted.h"
#include "base/memory/weak_ptr.h"
#include "components/tab_groups/tab_group_visual_data.h"
#include "ui/gfx/image/image_skia.h"
#include "uw/components/tab_organization/page_tree.h"

namespace content {
class WebContents;
}

namespace uw {

class TabOrganizationController;

// Shows a workspace's tabs in every window on it. Off until the last stage of
// the shared tabs plan, T254.
BASE_DECLARE_FEATURE(kUwSharedTabs);

// The tab tree of one workspace and what goes with it: collapsed branches,
// kept and held tabs, and the names uw gave tasks. Every window showing the
// workspace has a tab for each page. One of them holds the page's live page;
// the others hold a copy of its history with no renderer. A change one window
// makes reaches the others at once. A window with no workspace, or any window
// while kUwSharedTabs is off, has a tree of its own.
class SharedTree : public base::RefCounted<SharedTree> {
 public:
  SharedTree();
  SharedTree(const SharedTree&) = delete;
  SharedTree& operator=(const SharedTree&) = delete;

  // The tree of `workspace`, made on first use. A workspace belongs to one
  // profile, so its id is enough.
  static scoped_refptr<SharedTree> For(const std::string& workspace);

  PageTree& tree() { return tree_; }
  std::set<PageId>& collapsed() { return collapsed_; }
  // Kept tabs, each with the task it was kept in.
  std::map<PageId, std::string>& kept() { return kept_; }
  // Held tabs, each with the parent it was nested under.
  std::map<PageId, PageId>& held() { return held_; }
  // The name uw gave each task.
  std::map<std::string, std::u16string>& generated_names() {
    return generated_names_;
  }
  // Pages opened by a link or a new-tab command in a window on the
  // workspace, as opposed to restored or dragged in.
  std::set<PageId>& opened() { return opened_; }

  // What Undo takes back: the tree and the tasks' looks before an action,
  // the title of the first tab it moved, and the task it moved them into,
  // empty for the Scratchpad.
  struct Step {
    Step();
    Step(Step&&);
    Step& operator=(Step&&);
    ~Step();
    PageTree tree;
    std::map<std::string, tab_groups::TabGroupVisualData> tasks;
    std::u16string tab;
    std::optional<std::string> task;
  };
  // The workspace's actions Undo can take back, the last one last.
  std::vector<Step>& undo() { return undo_; }

  // The windows on the tree, the first to join first.
  const std::vector<raw_ptr<TabOrganizationController>>& windows() const {
    return windows_;
  }
  void Add(TabOrganizationController* window);
  void Remove(TabOrganizationController* window);
  // Takes in `other`'s pages, after this tree's own in each place.
  void Merge(SharedTree& other);

  // Brings every other window up to date with the tree once it has changed,
  // unless a drag is under way: Chromium ends a drag when a tab strip gains
  // a tab. The window that drags brings them up to date when it ends.
  void Propagate(TabOrganizationController* from);
  // Has every window save its tabs and redraw them.
  void Touch();
  // The look of `task` in the first window but `except` that shows it.
  std::optional<tab_groups::TabGroupVisualData> VisualsOf(
      const std::string& task,
      const TabOrganizationController* except) const;
  // Gives `task` the look `from` gave it, in every other window.
  void SetVisuals(TabOrganizationController* from,
                  const std::string& task,
                  const tab_groups::TabGroupVisualData& visuals);

  // A copy of `live` to stand in for it in another window.
  static std::unique_ptr<content::WebContents> MakeCopy(
      content::WebContents* live);
  static bool IsCopy(content::WebContents* page);
  // Page `id` in a window but `except`: its live page where one holds it.
  content::WebContents* PageOf(const PageId& id,
                               const TabOrganizationController* except) const;
  // The window but `except` holding page `id`'s live page, or null.
  TabOrganizationController* HolderOf(
      const PageId& id,
      const TabOrganizationController* except) const;
  // Moves page `id`'s live page into `window`'s copy of it, leaving a copy
  // where it was. Loads the copy when no window holds the live page.
  void BringLivePage(TabOrganizationController* window, const PageId& id);
  // Gives the copies of page `id` its live page's history, title, and icon, a
  // moment after the live page changes them.
  void ScheduleRefresh(const PageId& id);
  // Page `id`'s picture as it last left a window, empty if there is none.
  gfx::ImageSkia PictureOf(const PageId& id) const;

 private:
  friend class base::RefCounted<SharedTree>;
  ~SharedTree();

  void Refresh(const PageId& id);
  void SetPicture(const PageId& id, gfx::ImageSkia picture);

  // The workspace this tree is listed under, empty for a window's own.
  std::string workspace_;
  PageTree tree_;
  std::set<PageId> collapsed_;
  std::map<PageId, std::string> kept_;
  std::map<PageId, PageId> held_;
  std::map<std::string, std::u16string> generated_names_;
  std::set<PageId> opened_;
  std::vector<Step> undo_;
  std::vector<raw_ptr<TabOrganizationController>> windows_;
  // The tree as the other windows last followed it.
  PageTree propagated_;
  bool propagating_ = false;
  std::set<PageId> refreshing_;
  std::map<PageId, gfx::ImageSkia> pictures_;
  base::WeakPtrFactory<SharedTree> weak_factory_{this};
};

}  // namespace uw

#endif  // UW_BROWSER_TAB_ORGANIZATION_SHARED_TREE_H_
