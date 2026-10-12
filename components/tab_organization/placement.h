// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#ifndef UW_COMPONENTS_TAB_ORGANIZATION_PLACEMENT_H_
#define UW_COMPONENTS_TAB_ORGANIZATION_PLACEMENT_H_

#include <string>

#include "uw/components/tab_organization/ai_protocol.h"

namespace uw {

// How sure the model must be before uw acts on its answer without asking.
struct Thresholds {
  // Of the task a branch belongs to, or that it belongs to none. Also of a
  // task needing a new name.
  double confidence;
  // The probability that a branch outside every task starts one of its own.
  // Below it, the branch is a one-off for the Scratchpad.
  double new_task;
};

// The eagerness setting: how readily uw moves a branch. Each level up lowers
// the thresholds.
inline constexpr int kMinEagerness = 1;
inline constexpr int kMaxEagerness = 5;
inline constexpr int kDefaultEagerness = 3;

// The thresholds at `eagerness`. A level outside the range counts as the
// nearest one in it.
Thresholds ThresholdsFor(int eagerness);

// Where a branch sits when its answer arrives.
struct Position {
  // The option key of the named task it is in. Empty in the Scratchpad and in
  // a task that has no name yet.
  std::string task;
  // True in a task that has no name yet. The model was not told about that
  // task, so its answer cannot say the branch does not fit there. A branch
  // that starts a task gives this one its name.
  bool in_unnamed_task = false;
  // False for a branch that must not be taken out of its task without the
  // user.
  bool may_leave = true;
};

// What uw does with one placement answer.
struct Placement {
  enum class Action {
    // The branch stays where it is.
    kLeave,
    // The answer fell short of what a move needs. The user may confirm it.
    kSuggest,
    kApply,
    // The branch stays, and the task it is in gets a name: its first, or a
    // new one because it has outgrown the old.
    kName,
  };
  Action action = Action::kLeave;
  // A task option key, kNewTaskOption, or kScratchpadOption.
  std::string target;
  // The number that decided it, and what it had to reach.
  double confidence = 0.0;
  double needed = 0.0;
};

// Decides what to do with `answer` for a branch at `position`.
Placement Place(const ClassificationAnswer& answer,
                const Position& position,
                const Thresholds& needed);

}  // namespace uw

#endif  // UW_COMPONENTS_TAB_ORGANIZATION_PLACEMENT_H_
