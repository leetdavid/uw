// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#ifndef UW_COMPONENTS_TAB_ORGANIZATION_PLACEMENT_H_
#define UW_COMPONENTS_TAB_ORGANIZATION_PLACEMENT_H_

#include <string>

#include "uw/components/tab_organization/ai_protocol.h"

namespace uw {

// A move into a task or out of one needs the model this sure of where the
// branch belongs.
inline constexpr double kMinConfidence = 0.90;
// A branch that belongs to no task starts one of its own when the model gives
// that at least this probability. Below it, the branch is a one-off for the
// Scratchpad.
inline constexpr double kMinNewTaskProbability = 0.75;

// What uw does with one placement answer.
struct Placement {
  enum class Action {
    // The branch stays where it is.
    kLeave,
    // The answer fell short of what a move needs. The user may confirm it.
    kSuggest,
    kApply,
  };
  Action action = Action::kLeave;
  // A task option key, kNewTaskOption, or kScratchpadOption.
  std::string target;
  // The number that decided it, and what it had to reach.
  double confidence = 0.0;
  double needed = kMinConfidence;
};

// Decides what to do with `answer` for a branch that sits in `task`. `task`
// is a task option key, or empty for the Scratchpad and for a task the model
// was not told about. `may_leave` is false for a branch that must not be
// taken out of its task without the user.
Placement Place(const ClassificationAnswer& answer,
                const std::string& task,
                bool may_leave);

}  // namespace uw

#endif  // UW_COMPONENTS_TAB_ORGANIZATION_PLACEMENT_H_
