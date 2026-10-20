// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#ifndef UW_COMPONENTS_TAB_ORGANIZATION_PLACEMENT_H_
#define UW_COMPONENTS_TAB_ORGANIZATION_PLACEMENT_H_

#include <map>
#include <optional>
#include <string>
#include <vector>

#include "uw/components/tab_organization/ai_protocol.h"
#include "uw/components/tab_organization/page_tree.h"

namespace uw {

// How sure the model must be before uw acts on its answer without asking.
struct Thresholds {
  // Of the task a branch belongs to, or that it belongs to none.
  double confidence;
  // The probability that a branch outside every task starts one of its own.
  // Below it, the branch is a one-off for the Scratchpad.
  double new_task;
  // The probability that the task a branch is in has outgrown its name.
  double new_name;
};

// The thresholds until the user sets their own, and the range a setting can
// take. Half is the least a probability between two answers can ask for.
inline constexpr Thresholds kDefaultThresholds{0.70, 0.55, 0.70};
inline constexpr double kMinThreshold = 0.50;
inline constexpr double kMaxThreshold = 0.95;

// `value` as a setting: inside the range, in hundredths.
double AsThreshold(double value);

// Where a branch sits when its answer arrives.
struct Position {
  // The option key of the named task it is in. Empty in the Scratchpad and in
  // a task that has no name yet.
  std::string task;
  // True in a task that has no name yet. The model was not told about that
  // task, so its answer cannot say the branch does not fit there. A branch
  // that starts a task gives this one its name.
  bool in_unnamed_task = false;
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

  friend bool operator==(const Placement&, const Placement&) = default;
};

// True when the model finds it more likely than not that the tab shows a hub:
// a page the user comes back to for many things.
bool IsHub(const ClassificationAnswer& answer);

// Decides what to do with `answer` for a branch at `position`. A hub outside
// every task is a one-off whatever else the model says: it starts no task
// and gives none a name. A hub in a task stays there.
Placement Place(const ClassificationAnswer& answer,
                const Position& position,
                const Thresholds& needed);

// What uw knows about one tab when it decides about the branch the tab is in.
struct TabAnswer {
  // The model's last answer about what the tab shows now. None for a tab that
  // was not asked about, or whose request failed.
  std::optional<ClassificationAnswer> answer;
  // True for a tab that stays where it is whatever was answered: the user
  // placed it by hand, or uw has already acted on its answer.
  bool placed = false;
};

// What uw does with some of the tabs of one branch.
struct BranchPlacement {
  // The tab that decides it.
  PageId top;
  // `top` and the tabs under it that go where it goes, in tree order.
  std::vector<PageId> pages;
  // Tabs directly under one of `pages` that do not go along. When `pages`
  // move, these stay behind.
  std::vector<PageId> staying;
  Placement placement;

  friend bool operator==(const BranchPlacement&,
                         const BranchPlacement&) = default;
};

// Decides about the whole branch under `top` from the answers about its
// tabs. Tabs that sit under each other and whose answers put them in the same
// place go together: `top` of each group decides where, and whether its task
// has outgrown its name, while any tab of the group can make it the start of
// a task. A tab that fits where it is, or that is `placed`, stays behind. A
// tab without a usable answer goes with the page above it. A hub is never
// made into a task: it stays in the task it is in, or is filed on its own
// answer alone, for a task it fits or for the Scratchpad, and the tabs under
// it leave without it. `tabs` holds
// what is known about each tab; a tab missing from it was not asked about. The
// result lists every tab that has an answer, alone or in its group, in tree
// order.
std::vector<BranchPlacement> PlaceBranch(
    const PageTree& tree,
    const PageId& top,
    const std::map<PageId, TabAnswer>& tabs,
    const Position& position,
    const Thresholds& needed);

}  // namespace uw

#endif  // UW_COMPONENTS_TAB_ORGANIZATION_PLACEMENT_H_
