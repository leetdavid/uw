// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/components/tab_organization/placement.h"

#include <algorithm>
#include <cmath>

namespace uw {
namespace {

// Where a tab's own answer puts it, before the tabs around it are looked at.
struct Lean {
  enum class Kind {
    // No usable answer, or one the model is not sure of.
    kFollows,
    kStays,
    kLeaves,
  };
  Kind kind = Kind::kFollows;
  // For kLeaves: a task option key, or kNoTaskOption.
  std::string where;

  friend bool operator==(const Lean&, const Lean&) = default;
};

bool Outside(const ClassificationAnswer& answer) {
  return answer.choice == kNewTaskOption || answer.choice == kScratchpadOption;
}

// True when the model finds it more likely than not that the tab shows a hub:
// a page the user comes back to for many things.
bool Hub(const ClassificationAnswer& answer) {
  return answer.hub >= 0.5;
}

Lean LeanOf(const TabAnswer& tab,
            const Position& position,
            const Thresholds& needed) {
  if (tab.placed) {
    return {Lean::Kind::kStays};
  }
  if (!tab.answer) {
    return {};
  }
  const ClassificationAnswer& answer = *tab.answer;
  if (!Outside(answer) && answer.choice == position.task) {
    return {Lean::Kind::kStays};
  }
  // A hub never goes along with the page above it.
  if (answer.confidence < needed.confidence) {
    return {Hub(answer) ? Lean::Kind::kStays : Lean::Kind::kFollows};
  }
  if (!Outside(answer)) {
    return {Lean::Kind::kLeaves, answer.choice};
  }
  // Outside every task a hub is bound for the Scratchpad. That keeps it
  // apart from the tabs around it that may start a task.
  return {Lean::Kind::kLeaves, Hub(answer) ? kScratchpadOption : kNoTaskOption};
}

// The answer of a group of tabs that go together. Its top tab decides where
// they belong and whether their task has outgrown its name. Outside every
// task, the tab most likely to start a task decides what they are.
ClassificationAnswer JointAnswer(const BranchPlacement& group,
                                 const std::map<PageId, TabAnswer>& tabs) {
  ClassificationAnswer joint = *tabs.at(group.top).answer;
  if (!Outside(joint)) {
    return joint;
  }
  for (const PageId& id : group.pages) {
    const auto tab = tabs.find(id);
    if (tab == tabs.end() || !tab->second.answer ||
        !Outside(*tab->second.answer) || Hub(*tab->second.answer)) {
      continue;
    }
    const ClassificationAnswer& answer = *tab->second.answer;
    if (answer.new_task > joint.new_task) {
      joint.new_task = answer.new_task;
      joint.choice = answer.choice;
    }
  }
  return joint;
}

}  // namespace

double AsThreshold(double value) {
  return std::clamp(std::round(value * 100.0) / 100.0, kMinThreshold,
                    kMaxThreshold);
}

Placement Place(const ClassificationAnswer& answer,
                const Position& position,
                const Thresholds& needed) {
  using Action = Placement::Action;
  const bool sure = answer.confidence >= needed.confidence;
  // A hub outside every task is a one-off, whatever else the model says.
  const bool hub = Hub(answer) && Outside(answer);
  const bool starts_task = answer.choice == kNewTaskOption && !hub;
  const double new_task = hub ? 0.0 : answer.new_task;
  Placement placement{Action::kLeave,
                      hub ? std::string(kScratchpadOption) : answer.choice,
                      answer.confidence, needed.confidence};
  if (!Outside(answer)) {
    if (answer.choice != position.task) {
      placement.action = sure ? Action::kApply : Action::kSuggest;
    }
    return placement;
  }
  // A task without a name may be the very task this branch starts.
  if (position.in_unnamed_task) {
    return sure && new_task >= needed.new_task
               ? Placement{Action::kName, kNewTaskOption, new_task,
                           needed.new_task}
               : placement;
  }
  // A branch that fits no task name has left its task, unless it is the same
  // piece of work as the other tabs there: then the task has outgrown its
  // name. What the model calls a one-off says nothing about the task, so it
  // leaves either way.
  if (!position.task.empty() && starts_task &&
      answer.same_work >= needed.new_name) {
    return {Action::kName, answer.choice, answer.same_work, needed.new_name};
  }
  if (!sure) {
    if (starts_task || !position.task.empty()) {
      placement.action = Action::kSuggest;
    }
    return placement;
  }
  // The branch belongs to no task. What is left is whether it starts one.
  if (new_task >= needed.new_task) {
    return {Action::kApply, kNewTaskOption, new_task, needed.new_task};
  }
  if (!position.task.empty()) {
    placement.target = kScratchpadOption;
    placement.action = Action::kApply;
    return placement;
  }
  if (starts_task) {
    return {Action::kSuggest, kNewTaskOption, new_task, needed.new_task};
  }
  return placement;
}

std::vector<BranchPlacement> PlaceBranch(
    const PageTree& tree,
    const PageId& top,
    const std::map<PageId, TabAnswer>& tabs,
    const Position& position,
    const Thresholds& needed) {
  struct Seen {
    Lean lean;
    // Index in `placements` of the group the tab leaves with.
    std::optional<size_t> group;
  };
  std::vector<BranchPlacement> placements;
  std::vector<size_t> groups;
  std::map<PageId, Seen> seen;
  for (const PageId& id : tree.BranchOf(top)) {
    const auto tab = tabs.find(id);
    const Lean lean =
        tab == tabs.end() ? Lean() : LeanOf(tab->second, position, needed);
    const auto above = seen.find(tree.Find(id)->parent);
    const bool below_a_group = above != seen.end() && above->second.group;
    if (below_a_group &&
        (lean.kind == Lean::Kind::kFollows || lean == above->second.lean)) {
      placements[*above->second.group].pages.push_back(id);
      seen[id] = above->second;
      continue;
    }
    if (below_a_group) {
      placements[*above->second.group].staying.push_back(id);
    }
    if (lean.kind == Lean::Kind::kLeaves) {
      seen[id] = {lean, placements.size()};
      groups.push_back(placements.size());
      placements.push_back({id, {id}, {}, {}});
      continue;
    }
    seen[id] = {{Lean::Kind::kStays}, std::nullopt};
    if (tab != tabs.end() && tab->second.answer && !tab->second.placed) {
      placements.push_back(
          {id, {id}, {}, Place(*tab->second.answer, position, needed)});
    }
  }
  for (size_t index : groups) {
    BranchPlacement& group = placements[index];
    group.placement = Place(JointAnswer(group, tabs), position, needed);
  }
  return placements;
}

}  // namespace uw
