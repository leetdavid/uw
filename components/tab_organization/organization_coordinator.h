// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#ifndef UW_COMPONENTS_TAB_ORGANIZATION_ORGANIZATION_COORDINATOR_H_
#define UW_COMPONENTS_TAB_ORGANIZATION_ORGANIZATION_COORDINATOR_H_

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "uw/components/tab_organization/tab_tree.h"

namespace uw {

// Applies validated automatic-classification results to a TabTree.
//
// The coordinator is provider-independent: the classification provider lives
// in browser integration. It owns request identity, supersession, and the
// staleness checks that keep a late or overridden result from moving the
// wrong branch. It never touches live pages; TabTree's ordering and undo do
// that.
class OrganizationCoordinator {
 public:
  // Minimum classification confidence for an automatic move or task creation.
  static constexpr double kMinConfidence = 0.90;

  enum class Decision { kLeave, kMoveToTask, kNewTask };

  struct Result {
    uint64_t request_id = 0;
    Decision decision = Decision::kLeave;
    // Destination for kMoveToTask: an existing task's top level.
    TreeId destination_task{""};
    double confidence = 0.0;
    // Generated name for kNewTask. Empty keeps the provisional title.
    std::u16string task_name;
  };

  explicit OrganizationCoordinator(TabTree* tree);
  ~OrganizationCoordinator();
  OrganizationCoordinator(const OrganizationCoordinator&) = delete;
  OrganizationCoordinator& operator=(const OrganizationCoordinator&) = delete;

  // Captures the branch rooted at each normalized id and returns a request
  // id. 0 means there was nothing eligible to classify. A new request for an
  // overlapping branch supersedes older pending ones. The caller invalidates
  // the request when the branch's activity changes.
  uint64_t RequestClassification(const std::vector<TreeId>& branch_roots);

  // Applies a validated result. Returns the affected task — the destination
  // for kMoveToTask, the created task for kNewTask — or nullopt for leaves
  // and discarded results. Low-confidence results do nothing. The generated
  // name is applied as part of the same undoable action as task creation.
  std::optional<TreeId> ApplyResult(const Result& result);

  void Invalidate(const std::vector<TreeId>& ids);
  void InvalidateAll();

 private:
  struct Pending {
    std::vector<TreeId> roots;
    // Parent of each root at request time, in the same order.
    std::vector<TreeId> parents;
    // Manual version of each root plus its ancestors at request time.
    std::vector<std::vector<uint64_t>> manual_chain;
  };

  std::vector<uint64_t> ChainVersions(const TreeId& id) const;
  bool RootsStillCurrent(const Pending& pending) const;

  raw_ptr<TabTree> tree_;
  uint64_t next_request_id_ = 1;
  std::map<uint64_t, Pending> pending_;
};

}  // namespace uw

#endif  // UW_COMPONENTS_TAB_ORGANIZATION_ORGANIZATION_COORDINATOR_H_
