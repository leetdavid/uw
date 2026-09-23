// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/components/tab_organization/tab_tree.h"

#include <algorithm>
#include <set>
#include <utility>

#include "base/functional/bind.h"
#include "base/strings/utf_string_conversions.h"
#include "base/uuid.h"
#include "components/strings/grit/components_strings.h"
#include "components/undo/undo_operation.h"

namespace uw {
namespace {

using Node = TabTree::Node;
using Kind = TabTree::Kind;

TreeId NewId() {
  return TreeId(base::Uuid::GenerateRandomV4().AsLowercaseString());
}

void Preorder(const Node* node, std::vector<const Node*>& nodes) {
  nodes.push_back(node);
  for (const auto& child : node->children()) {
    Preorder(child.get(), nodes);
  }
}

bool Accepts(const Node* parent, Kind kind) {
  if (kind == Kind::kTask) {
    return parent->value.kind == Kind::kRoot ||
           parent->value.kind == Kind::kTask;
  }
  return kind == Kind::kPage && parent->value.kind != Kind::kRoot;
}

bool ValidSavedId(const std::string* id) {
  return id && base::Uuid::ParseLowercase(*id).is_valid();
}

size_t SavedPosition(const base::DictValue& state) {
  return static_cast<size_t>(std::max(0, state.FindInt("position").value_or(0)));
}

class TreeUndo : public UndoOperation {
 public:
  explicit TreeUndo(base::OnceClosure action) : action_(std::move(action)) {}
  void Undo() override { std::move(action_).Run(); }
  int GetUndoLabelId() const override { return IDS_BOOKMARK_BAR_UNDO; }
  int GetRedoLabelId() const override { return IDS_BOOKMARK_BAR_REDO; }

 private:
  base::OnceClosure action_;
};

}  // namespace

TabTree::TabTree(std::u16string scratchpad_title)
    : model_(std::make_unique<Node>(u"", Data{TreeId("root"), Kind::kRoot})) {
  nodes_.emplace(root()->value.id, root());
  Add(Kind::kScratchpad, std::move(scratchpad_title), root(), 0,
      TreeId("scratchpad"));
}

TabTree::~TabTree() = default;

TabTree::Node* TabTree::Find(const TreeId& id) {
  auto it = nodes_.find(id);
  return it == nodes_.end() ? nullptr : it->second;
}

const TabTree::Node* TabTree::Find(const TreeId& id) const {
  auto it = nodes_.find(id);
  return it == nodes_.end() ? nullptr : it->second;
}

TabTree::Node* TabTree::TaskFor(const TreeId& id) {
  for (Node* node = Find(id); node; node = node->parent()) {
    if (node->value.kind == Kind::kTask) return node;
  }
  return nullptr;
}

TabTree::Node* TabTree::Add(Kind kind, std::u16string title, Node* parent,
                           size_t index, std::optional<TreeId> id) {
  auto node = std::make_unique<Node>(title, Data{id.value_or(NewId()), kind});
  Node* result = model_.Add(parent, std::move(node),
                            std::min(index, parent->children().size()));
  nodes_.emplace(result->value.id, result);
  return result;
}

TreeId TabTree::AddPage(std::u16string title) {
  Node* node = Add(Kind::kPage, std::move(title), scratchpad(),
                   scratchpad()->children().size());
  Changed();
  return node->value.id;
}

void TabTree::UpdatePageTitle(const TreeId& id, std::u16string title) {
  Node* node = Find(id);
  if (!node || node->value.kind != Kind::kPage || node->GetTitle() == title)
    return;
  model_.SetTitle(node, title);
  Changed();
}

void TabTree::SetPageParent(const TreeId& page, const TreeId& parent) {
  Node* node = Find(page);
  Node* destination = Find(parent);
  if (!node || !destination || node->value.kind != Kind::kPage ||
      destination->value.kind != Kind::kPage ||
      destination->HasAncestor(node)) return;
  Reparent(node, destination, destination->children().size(), false);
  Changed();
}

void TabTree::RemoveAndPromote(Node* node) {
  Node* parent = node->parent();
  size_t index = parent->GetIndexOf(node).value();
  while (!node->children().empty()) {
    Node* child = node->children().front().get();
    Node* destination = Accepts(parent, child->value.kind) ? parent : scratchpad();
    Reparent(child, destination,
             destination == parent ? index++ : destination->children().size(),
             false);
  }
  nodes_.erase(node->value.id);
  pending_parents_.erase(node->value.id);
  model_.Remove(parent, node);
}

void TabTree::RemovePage(const TreeId& id) {
  Node* node = Find(id);
  if (!node || node->value.kind != Kind::kPage) return;
  RemoveAndPromote(node);
  Changed();
}

std::vector<TreeId> TabTree::Normalize(const std::vector<TreeId>& ids) const {
  const std::set<TreeId> selected(ids.begin(), ids.end());
  std::vector<const Node*> ordered;
  Preorder(root(), ordered);
  std::vector<TreeId> result;
  for (const Node* node : ordered) {
    if (!selected.contains(node->value.id) ||
        node->value.kind == Kind::kRoot ||
        node->value.kind == Kind::kScratchpad) continue;
    bool covered = false;
    for (const Node* parent = node->parent(); parent; parent = parent->parent()) {
      if (selected.contains(parent->value.id)) { covered = true; break; }
    }
    if (!covered) result.push_back(node->value.id);
  }
  return result;
}

std::vector<TreeId> TabTree::PagesIn(const std::vector<TreeId>& ids) const {
  std::vector<TreeId> result;
  for (const auto& id : Normalize(ids)) {
    std::vector<const Node*> branch;
    Preorder(Find(id), branch);
    for (const Node* node : branch) {
      if (node->value.kind == Kind::kPage) result.push_back(node->value.id);
    }
  }
  return result;
}

std::vector<TreeId> TabTree::PageOrder() const {
  std::vector<const Node*> ordered;
  Preorder(root(), ordered);
  std::vector<TreeId> result;
  for (const Node* node : ordered) {
    if (node->value.kind == Kind::kPage) result.push_back(node->value.id);
  }
  return result;
}

bool TabTree::CanMove(const std::vector<TreeId>& ids, const TreeId& parent) const {
  const Node* destination = Find(parent);
  const auto roots = Normalize(ids);
  if (!destination || roots.empty()) return false;
  for (const auto& id : roots) {
    const Node* node = Find(id);
    if (!Accepts(destination, node->value.kind) || destination->HasAncestor(node))
      return false;
  }
  return true;
}

TabTree::Placement TabTree::Remember(Node* node) const {
  Placement result;
  result.id = node->value.id;
  result.expected_version = node->value.move_version + 1;
  for (Node* parent = node->parent(); parent; parent = parent->parent())
    result.parents.push_back(parent->value.id);
  const auto& siblings = node->parent()->children();
  const size_t index = node->parent()->GetIndexOf(node).value();
  for (size_t i = index + 1; i < siblings.size(); ++i)
    result.following.push_back(siblings[i]->value.id);
  for (size_t i = index; i > 0; --i)
    result.preceding.push_back(siblings[i - 1]->value.id);
  return result;
}

void TabTree::Reparent(Node* node, Node* parent, size_t index, bool manual) {
  Node* old_parent = node->parent();
  const size_t old_index = old_parent->GetIndexOf(node).value();
  if (old_parent == parent && old_index < index) --index;
  auto owned = model_.Remove(old_parent, old_index);
  model_.Add(parent, std::move(owned), std::min(index, parent->children().size()));
  if (manual) ++node->value.move_version;
}

bool TabTree::Move(const std::vector<TreeId>& ids, const TreeId& parent,
                   std::optional<TreeId> before) {
  if (!CanMove(ids, parent)) return false;
  auto roots = Normalize(ids);
  Node* destination = Find(parent);
  Node* anchor = before ? Find(*before) : nullptr;
  if (destination == root() && anchor == scratchpad()) return false;
  if (before && (!anchor || anchor->parent() != destination ||
      std::ranges::find(roots, *before) != roots.end())) return false;
  std::vector<Placement> placements;
  for (const auto& id : roots) placements.push_back(Remember(Find(id)));
  for (const auto& id : roots) {
    Reparent(Find(id), destination,
             anchor ? destination->GetIndexOf(anchor).value()
                    : destination->children().size(), true);
  }
  Record(base::BindOnce(&TabTree::UndoMove, weak_factory_.GetWeakPtr(),
                        std::move(placements), std::nullopt));
  Changed();
  return true;
}

std::optional<TreeId> TabTree::MakeTask(const std::vector<TreeId>& pages,
                                       std::u16string title) {
  const auto roots = Normalize(pages);
  if (roots.empty() || std::ranges::any_of(roots, [this](const TreeId& id) {
        return Find(id)->value.kind != Kind::kPage;
      })) return std::nullopt;
  Node* source_task = TaskFor(roots.front());
  Node* parent = source_task ? source_task->parent() : root();
  const size_t index = source_task ? parent->GetIndexOf(source_task).value() + 1 : 1;
  std::vector<Placement> placements;
  for (const auto& id : roots) placements.push_back(Remember(Find(id)));
  Node* task = Add(Kind::kTask, std::move(title), parent, index);
  const TreeId task_id = task->value.id;
  for (const auto& id : roots)
    Reparent(Find(id), task, task->children().size(), true);
  Record(base::BindOnce(&TabTree::UndoMove, weak_factory_.GetWeakPtr(),
                        std::move(placements), task_id));
  Changed();
  return task_id;
}

void TabTree::UndoMove(std::vector<Placement> placements,
                       std::optional<TreeId> created_task) {
  for (auto it = placements.rbegin(); it != placements.rend(); ++it) {
    Node* node = Find(it->id);
    if (!node || node->value.move_version != it->expected_version) continue;
    Node* destination = node->value.kind == Kind::kTask ? root() : scratchpad();
    for (const auto& id : it->parents) {
      Node* candidate = Find(id);
      if (candidate && Accepts(candidate, node->value.kind) &&
          !candidate->HasAncestor(node)) { destination = candidate; break; }
    }
    size_t index = destination->children().size();
    bool anchored = false;
    for (const auto& id : it->following) {
      Node* anchor = Find(id);
      if (anchor && anchor->parent() == destination && anchor != node) {
        index = destination->GetIndexOf(anchor).value();
        anchored = true;
        break;
      }
    }
    if (!anchored) {
      for (const auto& id : it->preceding) {
        Node* anchor = Find(id);
        if (anchor && anchor->parent() == destination && anchor != node) {
          index = destination->GetIndexOf(anchor).value() + 1;
          break;
        }
      }
    }
    // The fixed Scratchpad header always remains first at the root.
    if (destination == root()) index = std::max<size_t>(1, index);
    Reparent(node, destination, index, false);
    --node->value.move_version;
  }
  if (created_task) {
    if (Node* node = Find(*created_task)) RemoveAndPromote(node);
  }
  Changed();
}

void TabTree::RenameTask(const TreeId& id, std::u16string title) {
  Node* node = Find(id);
  if (!node || node->value.kind != Kind::kTask || title.empty() ||
      node->GetTitle() == title) return;
  const std::u16string old_title = node->GetTitle();
  model_.SetTitle(node, title);
  Record(base::BindOnce(&TabTree::UndoRename, weak_factory_.GetWeakPtr(),
                        id, title, old_title));
  Changed();
}

void TabTree::UndoRename(TreeId id, std::u16string expected,
                         std::u16string previous) {
  Node* node = Find(id);
  if (node && node->GetTitle() == expected) {
    model_.SetTitle(node, previous);
    Changed();
  }
}

void TabTree::ToggleCollapsed(const TreeId& id) {
  Node* node = Find(id);
  if (!node || node->value.kind == Kind::kRoot ||
      node->value.kind == Kind::kScratchpad) return;
  node->value.collapsed = !node->value.collapsed;
  Changed();
}

void TabTree::RemoveEmptyTask(const TreeId& id) {
  Node* node = Find(id);
  if (!node || node->value.kind != Kind::kTask || !node->children().empty()) return;
  RemoveAndPromote(node);
  Changed();
}

void TabTree::Record(base::OnceClosure inverse) {
  undo_.AddUndoOperation(std::make_unique<TreeUndo>(std::move(inverse)));
}

void TabTree::Undo() {
  if (CanUndo()) undo_.Undo();
}

base::DictValue TabTree::SavePage(const TreeId& id) const {
  base::DictValue result;
  const Node* node = Find(id);
  if (!node || node->value.kind != Kind::kPage) return result;
  result.Set("version", 1);
  result.Set("id", id.value());
  result.Set("collapsed", node->value.collapsed);
  result.Set("position", static_cast<int>(node->parent()->GetIndexOf(node).value()));
  if (node->parent()->value.kind == Kind::kPage)
    result.Set("page_parent", node->parent()->value.id.value());
  std::vector<const Node*> tasks;
  for (const Node* parent = node->parent(); parent; parent = parent->parent()) {
    if (parent->value.kind == Kind::kTask) tasks.push_back(parent);
  }
  base::ListValue path;
  for (auto it = tasks.rbegin(); it != tasks.rend(); ++it) {
    base::DictValue task;
    task.Set("id", (*it)->value.id.value());
    task.Set("title", base::UTF16ToUTF8((*it)->GetTitle()));
    task.Set("collapsed", (*it)->value.collapsed);
    task.Set("position", static_cast<int>((*it)->parent()->GetIndexOf(*it).value()));
    path.Append(std::move(task));
  }
  result.Set("tasks", std::move(path));
  return result;
}

TreeId TabTree::RestorePage(const base::DictValue& state,
                            std::u16string title) {
  const std::string* saved_id = state.FindString("id");
  const auto* tasks = state.FindList("tasks");
  if (state.FindInt("version") != 1 || !ValidSavedId(saved_id) ||
      Find(TreeId(*saved_id)) || !tasks || tasks->size() > 100)
    return AddPage(std::move(title));
  // Validate the complete path before creating any node.
  std::set<std::string> ids{*saved_id};
  for (const auto& value : *tasks) {
    const auto* task = value.GetIfDict();
    if (!task || !ValidSavedId(task->FindString("id")) ||
        !task->FindString("title") ||
        !ids.insert(*task->FindString("id")).second)
      return AddPage(std::move(title));
    const Node* existing = Find(TreeId(*task->FindString("id")));
    if (existing && existing->value.kind != Kind::kTask)
      return AddPage(std::move(title));
  }
  Node* parent = root();
  for (const auto& value : *tasks) {
    const auto& task = value.GetDict();
    const TreeId id(*task.FindString("id"));
    Node* next = Find(id);
    if (!next) {
      next = Add(Kind::kTask, base::UTF8ToUTF16(*task.FindString("title")),
                  parent, parent == root() ? std::max<size_t>(1, SavedPosition(task))
                                           : SavedPosition(task), id);
      next->value.collapsed = task.FindBool("collapsed").value_or(false);
    }
    parent = next;
  }
  if (parent == root()) parent = scratchpad();
  Node* page = Add(Kind::kPage, std::move(title), parent,
                   SavedPosition(state), TreeId(*saved_id));
  page->value.collapsed = state.FindBool("collapsed").value_or(false);
  if (const auto* page_parent = state.FindString("page_parent");
      ValidSavedId(page_parent) && *page_parent != *saved_id) {
    pending_parents_.emplace(page->value.id,
        PendingParent{TreeId(*page_parent), SavedPosition(state),
                      page->value.move_version});
  }
  Changed();
  return page->value.id;
}

void TabTree::ResolveRestoredParents() {
  for (auto it = pending_parents_.begin(); it != pending_parents_.end();) {
    Node* node = Find(it->first);
    Node* parent = Find(it->second.parent);
    if (!node || node->value.move_version != it->second.version) {
      it = pending_parents_.erase(it);
    } else if (parent) {
      if (parent->value.kind == Kind::kPage && !parent->HasAncestor(node) &&
          TaskFor(parent->value.id) == TaskFor(node->value.id))
        Reparent(node, parent, it->second.position, false);
      it = pending_parents_.erase(it);
    } else {
      ++it;
    }
  }
  Changed();
}

base::CallbackListSubscription TabTree::Observe(base::RepeatingClosure callback) {
  return changed_.Add(std::move(callback));
}

void TabTree::Changed() {
  changed_.Notify();
}

}  // namespace uw
