// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/components/tab_organization/organization_coordinator.h"

#include <algorithm>

namespace uw {

OrganizationCoordinator::OrganizationCoordinator(TabTree* tree)
    : tree_(tree) {}

OrganizationCoordinator::~OrganizationCoordinator() = default;

uint64_t OrganizationCoordinator::RequestClassification(
    const std::vector<TreeId>& branch_roots,
    uint64_t activity_version) {
  const std::vector<TreeId> roots = tree_->Normalize(branch_roots);
  if (roots.empty()) {
    return 0;
  }
  Pending pending;
  pending.activity_version = activity_version;
  for (const auto& id : roots) {
    const TabTree::Node* node = tree_->Find(id);
    if (!node || !node->parent()) {
      return 0;
    }
    pending.roots.push_back(id);
    pending.parents.push_back(node->parent()->value.id);
    pending.manual_chain.push_back(ChainVersions(id));
  }
  // A new request supersedes pending ones covering the same branch.
  for (auto it = pending_.begin(); it != pending_.end();) {
    bool overlaps = false;
    for (const auto& id : it->second.roots) {
      if (std::ranges::find(roots, id) != roots.end() ||
          tree_->Normalize({id}) != std::vector<TreeId>{id}) {
        overlaps = true;
        break;
      }
    }
    // Also supersede when a pending request's root is now covered by one of
    // the new normalized roots (e.g. the caller passed a parent this time).
    for (const auto& id : roots) {
      for (const auto& old : it->second.roots) {
        const TabTree::Node* node = tree_->Find(old);
        for (const TabTree::Node* parent = node ? node->parent() : nullptr;
             parent; parent = parent->parent()) {
          if (parent->value.id == id) {
            overlaps = true;
            break;
          }
        }
      }
    }
    if (overlaps) {
      it = pending_.erase(it);
    } else {
      ++it;
    }
  }
  const uint64_t request_id = next_request_id_++;
  pending_[request_id] = std::move(pending);
  return request_id;
}

std::optional<TreeId> OrganizationCoordinator::ApplyResult(
    const Result& result) {
  auto it = pending_.find(result.request_id);
  if (it == pending_.end()) {
    return std::nullopt;
  }
  Pending pending = std::move(it->second);
  pending_.erase(it);
  if (result.decision == Decision::kLeave ||
      result.confidence < kMinConfidence) {
    return std::nullopt;
  }
  if (!RootsStillCurrent(pending)) {
    return std::nullopt;
  }
  if (result.decision == Decision::kMoveToTask) {
    const TabTree::Node* destination = tree_->Find(result.destination_task);
    if (!destination ||
        destination->value.kind != TabTree::Kind::kTask) {
      return std::nullopt;
    }
    if (!tree_->MoveAutomatic(pending.roots, result.destination_task)) {
      return std::nullopt;
    }
    return std::nullopt;
  }
  if (result.decision == Decision::kNewTask) {
    return tree_->MakeTaskAutomatic(pending.roots, u"New task");
  }
  return std::nullopt;
}

bool OrganizationCoordinator::ApplyNaming(const TreeId& task,
                                          std::u16string title,
                                          uint64_t expected_manual_version) {
  const TabTree::Node* node = tree_->Find(task);
  if (!node || node->value.kind != TabTree::Kind::kTask || title.empty()) {
    return false;
  }
  if (tree_->ManualVersion(task) != expected_manual_version) {
    return false;
  }
  tree_->SetAutomaticTaskTitle(task, std::move(title));
  return tree_->Find(task) != nullptr;
}

void OrganizationCoordinator::Invalidate(const std::vector<TreeId>& ids) {
  const std::vector<TreeId> roots = tree_->Normalize(ids);
  for (auto it = pending_.begin(); it != pending_.end();) {
    bool overlaps = false;
    for (const auto& id : it->second.roots) {
      if (std::ranges::find(roots, id) != roots.end()) {
        overlaps = true;
        break;
      }
      const TabTree::Node* node = tree_->Find(id);
      for (const TabTree::Node* parent = node ? node->parent() : nullptr;
           parent; parent = parent->parent()) {
        if (std::ranges::find(roots, parent->value.id) != roots.end()) {
          overlaps = true;
          break;
        }
      }
    }
    if (overlaps) {
      it = pending_.erase(it);
    } else {
      ++it;
    }
  }
}

void OrganizationCoordinator::InvalidateAll() {
  pending_.clear();
}

bool OrganizationCoordinator::HasPending(uint64_t request_id) const {
  return pending_.contains(request_id);
}

std::vector<uint64_t> OrganizationCoordinator::ChainVersions(
    const TreeId& id) const {
  std::vector<uint64_t> versions;
  for (const TabTree::Node* node = tree_->Find(id); node;
       node = node->parent()) {
    versions.push_back(tree_->ManualVersion(node->value.id));
  }
  return versions;
}

bool OrganizationCoordinator::RootsStillCurrent(
    const Pending& pending) const {
  if (pending.roots.size() != pending.parents.size() ||
      pending.roots.size() != pending.manual_chain.size()) {
    return false;
  }
  for (size_t i = 0; i < pending.roots.size(); ++i) {
    const TabTree::Node* node = tree_->Find(pending.roots[i]);
    if (!node || !node->parent() ||
        node->parent()->value.id != pending.parents[i]) {
      return false;
    }
    if (ChainVersions(pending.roots[i]) != pending.manual_chain[i]) {
      return false;
    }
  }
  // Roots must still normalize to themselves: a root absorbed into another
  // moved branch is stale.
  if (tree_->Normalize(pending.roots) != pending.roots) {
    return false;
  }
  return true;
}

}  // namespace uw
