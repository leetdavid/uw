// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#ifndef UW_COMPONENTS_TAB_ORGANIZATION_PAGE_TREE_H_
#define UW_COMPONENTS_TAB_ORGANIZATION_PAGE_TREE_H_

#include <cstddef>
#include <set>
#include <string>
#include <vector>

#include "base/types/strong_alias.h"

namespace uw {

using PageId = base::StrongAlias<class PageIdTag, std::string>;

// One regular tab: where it sits and which page opened it.
struct Page {
  PageId id;
  // Native tab group key. Empty means the Scratchpad.
  std::string task;
  // Empty means top level.
  PageId parent;

  friend bool operator==(const Page&, const Page&) = default;
};

// The regular tabs of one window in tab-strip order. Each task's pages are
// contiguous, Scratchpad pages come after every task, and a page's children
// follow it directly inside the same task. Every mutation restores those
// invariants. Tasks themselves are native tab groups; this holds only their
// keys.
class PageTree {
 public:
  PageTree();
  explicit PageTree(std::vector<Page> pages);
  PageTree(const PageTree&);
  PageTree& operator=(const PageTree&);
  PageTree(PageTree&&);
  PageTree& operator=(PageTree&&);
  ~PageTree();

  const std::vector<Page>& pages() const { return pages_; }
  const Page* Find(const PageId& id) const;
  // `id` followed by its descendants, in order.
  std::vector<PageId> BranchOf(const PageId& id) const;
  int DepthOf(const PageId& id) const;
  PageId TopOf(const PageId& id) const;
  // Task keys in display order.
  std::vector<std::string> Tasks() const;
  const Page* PreviousSibling(const PageId& id) const;

  // Takes the tab strip's order and task membership after a native change.
  // `strip` parent fields are ignored. Pages missing from `strip` are closed
  // and their children move up one level. A page keeps its nearest earlier
  // ancestor whose branch it still sits inside. A page in `moved`, or one new
  // to the tree, instead joins the branch it was dropped into.
  void Adopt(const std::vector<Page>& strip, const std::set<PageId>& moved);

  // A blank new tab: sibling directly below `active`'s branch.
  bool OpenBeside(const PageId& id, const PageId& active);
  // A link opened in a new tab, or a restored link: last child of `parent`.
  bool SetParent(const PageId& id, const PageId& parent);
  // Moves a branch to the end of an existing task, or of the Scratchpad when
  // `task` is empty, and detaches it from its parent.
  bool MoveToTask(const PageId& id, const std::string& task);
  // Moves a branch into the unused key `task`, placed after every other
  // task.
  bool MoveToNewTask(const PageId& id, const std::string& task);
  // Makes a branch the last child of its previous sibling.
  bool Nest(const PageId& id);
  // Moves a branch up one level, directly below its old parent's branch.
  bool Outdent(const PageId& id);

  // Puts every page that `earlier` also holds back where it was. Pages opened
  // since stay with their parent, or drop to the Scratchpad when their task
  // is gone.
  void Restore(const PageTree& earlier);

  friend bool operator==(const PageTree&, const PageTree&) = default;

 private:
  bool HasTask(const std::string& task) const;
  // Index just past the last page of `task`, or 0 when it has none.
  size_t EndOfTask(const std::string& task) const;
  // Index just past the last page of `ids` still present.
  size_t EndOf(const std::vector<PageId>& ids) const;
  // Removes the branch of `id` and returns it. `origin` receives the index
  // the branch started at among the remaining pages.
  std::vector<Page> Detach(const PageId& id, size_t* origin = nullptr);
  void Attach(std::vector<Page> branch,
              const std::string& task,
              const PageId& parent,
              size_t index);
  void Normalize();

  std::vector<Page> pages_;
};

}  // namespace uw

#endif  // UW_COMPONENTS_TAB_ORGANIZATION_PAGE_TREE_H_
