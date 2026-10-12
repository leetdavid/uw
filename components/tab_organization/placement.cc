// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/components/tab_organization/placement.h"

#include <algorithm>
#include <cmath>

namespace uw {

double AsThreshold(double value) {
  return std::clamp(std::round(value * 100.0) / 100.0, kMinThreshold,
                    kMaxThreshold);
}

Placement Place(const ClassificationAnswer& answer,
                const Position& position,
                const Thresholds& needed) {
  using Action = Placement::Action;
  const bool sure = answer.confidence >= needed.confidence;
  const bool starts_task = answer.choice == kNewTaskOption;
  const bool fits_a_task = !starts_task && answer.choice != kScratchpadOption;
  Placement placement{Action::kLeave, answer.choice, answer.confidence,
                      needed.confidence};
  if (fits_a_task && answer.choice != position.task) {
    placement.action = sure ? Action::kApply : Action::kSuggest;
    return placement;
  }
  const Placement renamed{Action::kName, answer.choice, answer.new_name,
                          needed.new_name};
  const bool outgrown =
      !position.task.empty() && answer.new_name >= needed.new_name;
  if (fits_a_task) {
    return outgrown ? renamed : placement;
  }
  // A task without a name may be the very task this branch starts.
  if (position.in_unnamed_task) {
    return sure && answer.new_task >= needed.new_task
               ? Placement{Action::kName, kNewTaskOption, answer.new_task,
                           needed.new_task}
               : placement;
  }
  // A branch that no longer matches the name of its task has left the task,
  // unless it is the task that has outgrown the name. What the model calls a
  // one-off says nothing about the task, so it leaves either way.
  if (outgrown && starts_task) {
    return renamed;
  }
  if (!sure) {
    if (starts_task || !position.task.empty()) {
      placement.action = Action::kSuggest;
    }
    return placement;
  }
  // The branch belongs to no task. What is left is whether it starts one.
  if (answer.new_task >= needed.new_task) {
    return {position.task.empty() || position.may_leave ? Action::kApply
                                                        : Action::kSuggest,
            kNewTaskOption, answer.new_task, needed.new_task};
  }
  if (!position.task.empty()) {
    placement.target = kScratchpadOption;
    placement.action = position.may_leave ? Action::kApply : Action::kSuggest;
    return placement;
  }
  if (starts_task) {
    return {Action::kSuggest, kNewTaskOption, answer.new_task, needed.new_task};
  }
  return placement;
}

}  // namespace uw
