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
// The coordinator is provider-independent: providers (Jev, naming LLM) live in
// browser integration. It owns request identity, supersession, and the
// staleness checks that keep a late or overridden result from moving the
// wrong branch. It never touches live pages; TabTreeses ordering and undo do
// that.
class OrganizationCoordinator {
 public:
  // Minimum Jev Choice confidence for an automatic move or task creation.
  static constexpr double kMinConfidence = 0.90;

  enum class Decision { kLeave, kMoveToTask, kNewTask };

  struct Result {
    uint64_t request_id = 0;
    Decision decision = Decision::kLeave;
    // Destination for kMoveToTask: an existing task's top level.
    TreeId destination_task{""};
    double confidence = 0.0;
    // Must match the activity revision captured at request time.
    uint64_t activity_version = 0;
  };

  explicit OrganizationCoordinator(TabTree* tree);
  ~OrganizationCoordinator();
  OrganizationCoordinator(const OrganizationCoordinator&) = delete;
  OrganizationCoordinator& operator=(const OrganizationCoordinator&) = delete;

  // Captures the branch rooted at each normalized id and returns a request
  // id. 0 means there was nothing eligible to classify. A new request for an
  // overlapping branch supersedes older pending ones.
  uint64_t RequestClassification(const std::vector<TreeId>& branch_roots,
                                 uint64_t activity_version);

  // Applies a validated result. Returns the created task for kNewTask, or
  // nullopt for moves, leaves, and discarded results. Low-confidence results
  // do nothing and never trigger naming.
  std::optional<TreeId> ApplyResult(const Result& result);

  // Applies a generated name to an automatically created task. Returns false
  // (leaving the provisional title) when the task is gone, is not a task, or
  // was manually renamed after `expected_manual_version` was captured.
  bool ApplyNaming(const TreeId& task,
                   std::u16string title,
                   uint64_t expected_manual_version);

  void Invalidate(const std::vector<TreeId>& ids);
  void InvalidateAll();
  bool HasPending(uint64_t request_id) const;

 private:
  struct Pending {
    std::vector<TreeId> roots;
    // Parent of each root at request time, in the same order.
    std::vector<TreeId> parents;
    // Manual version of each root plus its ancestors at request time.
    std::vector<std::vector<uint64_t>> manual_chain;
    uint64_t activity_version = 0;
  };

  std::vector<uint64_t> ChainVersions(const TreeId& id) const;
  bool RootsStillCurrent(const Pending& pending) const;

  raw_ptr<TabTree> tree_;
  uint64_t next_request_id_ = 1;
  std::map<uint64_t, Pending> pending_;
};

}  // namespace uw

#endif  // UW_COMPONENTS_TAB_ORGANIZATION_ORGANIZATION_COORDINATOR_H_
