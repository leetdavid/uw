// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/components/tab_organization/placement.h"

namespace uw {

Placement Place(const ClassificationAnswer& answer,
                const std::string& task,
                bool may_leave) {
  using Action = Placement::Action;
  const bool sure = answer.confidence >= kMinConfidence;
  const bool starts_task = answer.choice == kNewTaskOption;
  Placement placement{Action::kLeave, answer.choice, answer.confidence,
                      kMinConfidence};
  if (!starts_task && answer.choice != kScratchpadOption) {
    if (answer.choice != task) {
      placement.action = sure ? Action::kApply : Action::kSuggest;
    }
    return placement;
  }
  if (!sure) {
    if (starts_task || !task.empty()) {
      placement.action = Action::kSuggest;
    }
    return placement;
  }
  // The branch belongs to no task. What is left is whether it starts one.
  if (answer.new_task >= kMinNewTaskProbability) {
    return {task.empty() || may_leave ? Action::kApply : Action::kSuggest,
            kNewTaskOption, answer.new_task, kMinNewTaskProbability};
  }
  if (!task.empty()) {
    placement.target = kScratchpadOption;
    placement.action = may_leave ? Action::kApply : Action::kSuggest;
    return placement;
  }
  if (starts_task) {
    return {Action::kSuggest, kNewTaskOption, answer.new_task,
            kMinNewTaskProbability};
  }
  return placement;
}

}  // namespace uw
