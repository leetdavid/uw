// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#ifndef UW_COMPONENTS_TAB_ORGANIZATION_TAB_TREE_H_
#define UW_COMPONENTS_TAB_ORGANIZATION_TAB_TREE_H_

#include <map>
#include <optional>
#include <string>
#include <vector>

#include "base/callback_list.h"
#include "base/memory/weak_ptr.h"
#include "base/types/strong_alias.h"
#include "base/values.h"
#include "components/undo/undo_manager.h"
#include "ui/base/models/tree_node_model.h"

namespace uw {

using TreeId = base::StrongAlias<class TreeIdTag, std::string>;

// Hierarchy metadata only. Chromium remains responsible for live pages.
class TabTree {
 public:
  enum class Kind { kRoot, kScratchpad, kTask, kPage };
  struct Data {
    TreeId id;
    Kind kind;
    bool collapsed = false;
    uint64_t move_version = 0;
  };
  using Node = ui::TreeNodeWithValue<Data>;

  explicit TabTree(std::u16string scratchpad_title);
  ~TabTree();
  TabTree(const TabTree&) = delete;
  TabTree& operator=(const TabTree&) = delete;

  Node* root() { return model_.GetRoot(); }
  const Node* root() const { return const_cast<TabTree*>(this)->model_.GetRoot(); }
  Node* scratchpad() { return root()->children().front().get(); }
  const Node* scratchpad() const { return root()->children().front().get(); }
  Node* Find(const TreeId& id);
  const Node* Find(const TreeId& id) const;
  Node* TaskFor(const TreeId& id);

  TreeId AddPage(std::u16string title);
  void UpdatePageTitle(const TreeId& id, std::u16string title);
  // Capture explicit new-tab links. Not an organization action to undo.
  void SetPageParent(const TreeId& page, const TreeId& parent);
  // Called after a real page is removed, detached, or pinned. Promotes children.
  void RemovePage(const TreeId& id);

  std::vector<TreeId> Normalize(const std::vector<TreeId>& ids) const;
  std::vector<TreeId> PagesIn(const std::vector<TreeId>& ids) const;
  std::vector<TreeId> PageOrder() const;
  bool CanMove(const std::vector<TreeId>& ids, const TreeId& parent) const;
  bool Move(const std::vector<TreeId>& ids, const TreeId& parent,
            std::optional<TreeId> before = std::nullopt);
  std::optional<TreeId> MakeTask(const std::vector<TreeId>& pages,
                               std::u16string title);
  void RenameTask(const TreeId& id, std::u16string title);
  void ToggleCollapsed(const TreeId& id);
  void RemoveEmptyTask(const TreeId& id);
  bool CanUndo() const { return undo_.undo_count() != 0; }
  void Undo();

  // Store this with the page's Chromium session extra data. Task ancestors are
  // restored on demand; page ancestors bind only when their real tabs exist.
  base::DictValue SavePage(const TreeId& id) const;
  TreeId RestorePage(const base::DictValue& state, std::u16string title);
  void ResolveRestoredParents();

  base::CallbackListSubscription Observe(base::RepeatingClosure callback);

 private:
  struct Placement {
    TreeId id;
    std::vector<TreeId> parents;
    std::vector<TreeId> following;
    std::vector<TreeId> preceding;
    uint64_t expected_version = 0;
  };
  struct PendingParent {
    TreeId parent;
    size_t position;
    uint64_t version;
  };

  Node* Add(Kind kind, std::u16string title, Node* parent,
            size_t index, std::optional<TreeId> id = std::nullopt);
  Placement Remember(Node* node) const;
  void Reparent(Node* node, Node* parent, size_t index, bool manual);
  void RemoveAndPromote(Node* node);
  void UndoMove(std::vector<Placement> placements,
                std::optional<TreeId> created_task);
  void UndoRename(TreeId id, std::u16string expected, std::u16string previous);
  void Record(base::OnceClosure inverse);
  void Changed();

  ui::TreeNodeModel<Node> model_;
  std::map<TreeId, Node*> nodes_;
  std::map<TreeId, PendingParent> pending_parents_;
  UndoManager undo_;
  base::RepeatingClosureList changed_;
  base::WeakPtrFactory<TabTree> weak_factory_{this};
};

}  // namespace uw

#endif  // UW_COMPONENTS_TAB_ORGANIZATION_TAB_TREE_H_
