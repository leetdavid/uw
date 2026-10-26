// Copyright 2026 The uw Authors. All rights reserved.

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

// A hub shows what it is for only once the user does something in it, so in
// a task it stays there.
bool HubInATask(const ClassificationAnswer& answer, const Position& position) {
  return IsHub(answer) && (!position.task.empty() || position.in_unnamed_task);
}

// A branch leaves the task it is in only when the model also finds it a
// different piece of work from the other tabs there. A task without other
// tabs has no name check, and nothing holds the branch. Out of a task without
// a name, only a named task can take it.
bool SameWorkHolds(const ClassificationAnswer& answer,
                   const Position& position,
                   const Thresholds& needed) {
  const bool leaves =
      !position.task.empty() || (position.in_unnamed_task && !Outside(answer));
  return leaves && 1.0 - answer.same_work < needed.confidence;
}

Lean LeanOf(const TabAnswer& tab,
            const Position& position,
            const Thresholds& needed) {
  if (tab.held) {
    return {};
  }
  if (tab.placed) {
    return {Lean::Kind::kStays};
  }
  if (!tab.answer) {
    return {};
  }
  const ClassificationAnswer& answer = *tab.answer;
  if (HubInATask(answer, position) ||
      (!Outside(answer) && answer.choice == position.task)) {
    return {Lean::Kind::kStays};
  }
  // A hub never goes along with the page above it.
  if (answer.confidence < needed.confidence) {
    return {IsHub(answer) ? Lean::Kind::kStays : Lean::Kind::kFollows};
  }
  if (!Outside(answer)) {
    return {Lean::Kind::kLeaves, answer.choice};
  }
  // Outside every task a hub is bound for the Scratchpad. That keeps it
  // apart from the tabs around it that may start a task.
  return {Lean::Kind::kLeaves,
          IsHub(answer) ? kScratchpadOption : kNoTaskOption};
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
        !Outside(*tab->second.answer) || IsHub(*tab->second.answer)) {
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

bool IsHub(const ClassificationAnswer& answer) {
  return answer.hub >= 0.5;
}

double AsThreshold(double value) {
  return std::clamp(std::round(value * 100.0) / 100.0, kMinThreshold,
                    kMaxThreshold);
}

namespace {

// What uw does with `answer` before the name check can hold the branch.
Placement PlaceByName(const ClassificationAnswer& answer,
                      const Position& position,
                      const Thresholds& needed) {
  using Action = Placement::Action;
  if (HubInATask(answer, position)) {
    return {Action::kLeave, position.task, answer.confidence,
            needed.confidence};
  }
  const bool sure = answer.confidence >= needed.confidence;
  // A hub outside every task is a one-off, whatever else the model says.
  const bool hub = IsHub(answer) && Outside(answer);
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
  // A branch that fits no task name and is the same piece of work as the
  // other tabs of its task shows that the task has outgrown its name, if uw
  // gave it. What the model calls a one-off renames nothing.
  if (!position.task.empty() && !position.typed_name && starts_task &&
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

}  // namespace

Placement Place(const ClassificationAnswer& answer,
                const Position& position,
                const Thresholds& needed) {
  Placement placement = PlaceByName(answer, position, needed);
  if (placement.action == Placement::Action::kApply &&
      SameWorkHolds(answer, position, needed)) {
    placement.action = Placement::Action::kSuggest;
    placement.confidence = 1.0 - answer.same_work;
    placement.needed = needed.confidence;
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
    // A tab that the name check holds in its task stays, unless it goes
    // along with the page above it.
    if (lean.kind == Lean::Kind::kLeaves &&
        !SameWorkHolds(*tab->second.answer, position, needed)) {
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

std::vector<TidyMove> PlanTidy(const PageTree& tree,
                               const std::map<PageId, TidyTarget>& targets,
                               const std::set<PageId>& held) {
  // Pages come in tree order, so a held tab finds where the page above it
  // goes.
  std::map<PageId, TidyTarget> goes;
  const auto destination = [&goes](const PageId& id) {
    const auto found = goes.find(id);
    return found == goes.end() ? TidyTarget() : found->second;
  };
  for (const Page& page : tree.pages()) {
    const auto target = targets.find(page.id);
    if (target != targets.end()) {
      goes[page.id] =
          held.contains(page.id) ? destination(page.parent) : target->second;
    }
  }
  std::map<std::u16string, size_t> new_tasks;
  for (const auto& [id, target] : goes) {
    if (!target.new_task.empty()) {
      ++new_tasks[target.new_task];
    }
  }
  for (auto& [id, target] : goes) {
    if (!target.new_task.empty() && new_tasks[target.new_task] < 2) {
      target = TidyTarget();
    }
  }

  std::vector<TidyMove> moves;
  for (const Page& page : tree.pages()) {
    const TidyTarget target = destination(page.id);
    if (target == TidyTarget() ||
        (!page.parent.value().empty() && destination(page.parent) == target)) {
      continue;
    }
    TidyMove move{page.id, {}, target};
    std::set<PageId> along = {page.id};
    for (const PageId& id : tree.BranchOf(page.id)) {
      const PageId& parent = tree.Find(id)->parent;
      if (id == page.id || !along.contains(parent)) {
        continue;
      }
      if (destination(id) == target) {
        along.insert(id);
      } else {
        move.staying.push_back(id);
      }
    }
    moves.push_back(std::move(move));
  }
  return moves;
}

}  // namespace uw
