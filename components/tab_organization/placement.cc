// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/components/tab_organization/placement.h"

#include <algorithm>
#include <array>

namespace uw {
namespace {

constexpr std::array<Thresholds, kMaxEagerness> kThresholds = {{
    {0.90, 0.75},
    {0.80, 0.65},
    {0.70, 0.55},
    {0.60, 0.50},
    {0.50, 0.50},
}};

}  // namespace

Thresholds ThresholdsFor(int eagerness) {
  return kThresholds[std::clamp(eagerness, kMinEagerness, kMaxEagerness) -
                     kMinEagerness];
}

Placement Place(const ClassificationAnswer& answer,
                const Position& position,
                const Thresholds& needed) {
  using Action = Placement::Action;
  const bool sure = answer.confidence >= needed.confidence;
  const bool starts_task = answer.choice == kNewTaskOption;
  Placement placement{Action::kLeave, answer.choice, answer.confidence,
                      needed.confidence};
  if (!starts_task && answer.choice != kScratchpadOption) {
    if (answer.choice != position.task) {
      placement.action = sure ? Action::kApply : Action::kSuggest;
    }
    return placement;
  }
  // A task without a name may be the very task this branch starts.
  if (position.in_unnamed_task) {
    return placement;
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
