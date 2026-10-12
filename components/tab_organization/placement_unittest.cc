// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/components/tab_organization/placement.h"

#include "testing/gtest/include/gtest/gtest.h"

namespace uw {
namespace {

using Action = Placement::Action;

constexpr Thresholds kNeeded{0.90, 0.75};

ClassificationAnswer Answer(const std::string& choice,
                            double confidence,
                            double new_task = 0.0,
                            double new_name = 0.0) {
  ClassificationAnswer answer;
  answer.choice = choice;
  answer.confidence = confidence;
  answer.new_task = new_task;
  answer.new_name = new_name;
  return answer;
}

Placement PlaceIn(const std::string& task,
                  const ClassificationAnswer& answer,
                  bool may_leave = true) {
  return Place(answer, {task, /*in_unnamed_task=*/false, may_leave}, kNeeded);
}

TEST(PlacementTest, ABranchMovesToAnotherTaskOnlyWhenTheModelIsSure) {
  Placement placement = PlaceIn("task-1", Answer("task-2", 0.90));
  EXPECT_EQ(placement.action, Action::kApply);
  EXPECT_EQ(placement.target, "task-2");
  EXPECT_EQ(placement.confidence, 0.90);
  EXPECT_EQ(placement.needed, kNeeded.confidence);

  placement = PlaceIn(std::string(), Answer("task-2", 0.89));
  EXPECT_EQ(placement.action, Action::kSuggest);
  EXPECT_EQ(placement.target, "task-2");

  EXPECT_EQ(PlaceIn("task-1", Answer("task-1", 0.2)).action, Action::kLeave);
  EXPECT_EQ(PlaceIn("task-1", Answer("task-1", 0.99)).action, Action::kLeave);
}

TEST(PlacementTest, ABranchThatFitsNoTaskLeavesTheTaskItIsIn) {
  Placement placement = PlaceIn("task-1", Answer(kScratchpadOption, 0.95, 0.2));
  EXPECT_EQ(placement.action, Action::kApply);
  EXPECT_EQ(placement.target, kScratchpadOption);
  EXPECT_EQ(placement.confidence, 0.95);
  EXPECT_EQ(placement.needed, kNeeded.confidence);

  // Leaning towards a task of its own is not enough to start one, but the
  // branch still does not belong where it is.
  placement = PlaceIn("task-1", Answer(kNewTaskOption, 0.95, 0.6));
  EXPECT_EQ(placement.action, Action::kApply);
  EXPECT_EQ(placement.target, kScratchpadOption);

  placement = PlaceIn("task-1", Answer(kNewTaskOption, 0.95, 0.75));
  EXPECT_EQ(placement.action, Action::kApply);
  EXPECT_EQ(placement.target, kNewTaskOption);
  EXPECT_EQ(placement.confidence, 0.75);
  EXPECT_EQ(placement.needed, kNeeded.new_task);

  placement = PlaceIn("task-1", Answer(kScratchpadOption, 0.89, 0.2));
  EXPECT_EQ(placement.action, Action::kSuggest);
  EXPECT_EQ(placement.target, kScratchpadOption);
  EXPECT_EQ(placement.needed, kNeeded.confidence);
}

TEST(PlacementTest, ABranchThatMayNotLeaveIsOnlySuggested) {
  Placement placement =
      PlaceIn("task-1", Answer(kScratchpadOption, 0.99, 0.1), false);
  EXPECT_EQ(placement.action, Action::kSuggest);
  EXPECT_EQ(placement.target, kScratchpadOption);

  placement = PlaceIn("task-1", Answer(kNewTaskOption, 0.99, 0.9), false);
  EXPECT_EQ(placement.action, Action::kSuggest);
  EXPECT_EQ(placement.target, kNewTaskOption);

  // Joining another task is not leaving on the model's doubt about this one.
  EXPECT_EQ(PlaceIn("task-1", Answer("task-2", 0.95), false).action,
            Action::kApply);
}

TEST(PlacementTest, InTheScratchpadOnlyAClearNewTaskChangesAnything) {
  Placement placement =
      PlaceIn(std::string(), Answer(kNewTaskOption, 0.95, 0.8), false);
  EXPECT_EQ(placement.action, Action::kApply);
  EXPECT_EQ(placement.target, kNewTaskOption);

  placement = PlaceIn(std::string(), Answer(kNewTaskOption, 0.95, 0.6));
  EXPECT_EQ(placement.action, Action::kSuggest);
  EXPECT_EQ(placement.target, kNewTaskOption);
  EXPECT_EQ(placement.confidence, 0.6);
  EXPECT_EQ(placement.needed, kNeeded.new_task);

  placement = PlaceIn(std::string(), Answer(kNewTaskOption, 0.5, 0.99));
  EXPECT_EQ(placement.action, Action::kSuggest);
  EXPECT_EQ(placement.confidence, 0.5);
  EXPECT_EQ(placement.needed, kNeeded.confidence);

  EXPECT_EQ(PlaceIn(std::string(), Answer(kScratchpadOption, 0.99, 0.1)).action,
            Action::kLeave);
  EXPECT_EQ(PlaceIn(std::string(), Answer(kScratchpadOption, 0.3, 0.4)).action,
            Action::kLeave);
}

TEST(PlacementTest, InATaskWithoutANameABranchJoinsANamedTaskOrNamesItsOwn) {
  const Position unnamed{std::string(), /*in_unnamed_task=*/true};
  Placement placement = Place(Answer("task-2", 0.95), unnamed, kNeeded);
  EXPECT_EQ(placement.action, Action::kApply);
  EXPECT_EQ(placement.target, "task-2");
  EXPECT_EQ(Place(Answer("task-2", 0.5), unnamed, kNeeded).action,
            Action::kSuggest);

  // The task it is in is the one it starts, so that task gets its name.
  placement = Place(Answer(kNewTaskOption, 0.99, 0.75), unnamed, kNeeded);
  EXPECT_EQ(placement.action, Action::kName);
  EXPECT_EQ(placement.confidence, 0.75);
  EXPECT_EQ(placement.needed, kNeeded.new_task);

  // A one-off, or an answer the model is not sure of, names nothing.
  EXPECT_EQ(Place(Answer(kNewTaskOption, 0.99, 0.74), unnamed, kNeeded).action,
            Action::kLeave);
  EXPECT_EQ(Place(Answer(kNewTaskOption, 0.5, 0.99), unnamed, kNeeded).action,
            Action::kLeave);
  EXPECT_EQ(
      Place(Answer(kScratchpadOption, 0.99, 0.1), unnamed, kNeeded).action,
      Action::kLeave);
}

TEST(PlacementTest, ATaskThatHasOutgrownItsNameKeepsTheBranch) {
  // Without the name check this branch would start a task of its own.
  Placement placement =
      PlaceIn("task-1", Answer(kNewTaskOption, 0.95, 0.9, /*new_name=*/0.90));
  EXPECT_EQ(placement.action, Action::kName);
  EXPECT_EQ(placement.confidence, 0.90);
  EXPECT_EQ(placement.needed, kNeeded.confidence);
  EXPECT_EQ(PlaceIn("task-1", Answer(kNewTaskOption, 0.5, 0.9, 0.95)).action,
            Action::kName);
  EXPECT_EQ(PlaceIn("task-1", Answer(kNewTaskOption, 0.95, 0.9, 0.95),
                    /*may_leave=*/false)
                .action,
            Action::kName);

  // A branch that fits its task can still show that the name is too narrow.
  EXPECT_EQ(PlaceIn("task-1", Answer("task-1", 0.95, 0.0, 0.92)).action,
            Action::kName);
  EXPECT_EQ(PlaceIn("task-1", Answer("task-1", 0.95, 0.0, 0.89)).action,
            Action::kLeave);

  // Short of the threshold, it is the branch that has left the task.
  placement = PlaceIn("task-1", Answer(kNewTaskOption, 0.95, 0.9, 0.89));
  EXPECT_EQ(placement.action, Action::kApply);
  EXPECT_EQ(placement.target, kNewTaskOption);

  // A one-off says nothing about the task. It leaves for the Scratchpad.
  placement = PlaceIn("task-1", Answer(kScratchpadOption, 0.95, 0.1, 0.99));
  EXPECT_EQ(placement.action, Action::kApply);
  EXPECT_EQ(placement.target, kScratchpadOption);
  placement = PlaceIn("task-1", Answer(kNewTaskOption, 0.95, 0.74, 0.99));
  EXPECT_EQ(placement.action, Action::kApply);
  EXPECT_EQ(placement.target, kScratchpadOption);

  // Another task the branch belongs to comes first.
  placement = PlaceIn("task-1", Answer("task-2", 0.95, 0.0, 0.99));
  EXPECT_EQ(placement.action, Action::kApply);
  EXPECT_EQ(placement.target, "task-2");
  EXPECT_EQ(PlaceIn("task-1", Answer("task-2", 0.5, 0.0, 0.99)).action,
            Action::kSuggest);

  // The Scratchpad has no name to outgrow.
  EXPECT_EQ(
      PlaceIn(std::string(), Answer(kNewTaskOption, 0.95, 0.9, 0.99)).action,
      Action::kApply);
}

TEST(PlacementTest, EachEagernessLevelAsksForLess) {
  EXPECT_EQ(ThresholdsFor(kMinEagerness).confidence, 0.90);
  EXPECT_EQ(ThresholdsFor(kMinEagerness).new_task, 0.75);
  EXPECT_EQ(ThresholdsFor(kDefaultEagerness).confidence, 0.70);
  EXPECT_EQ(ThresholdsFor(kDefaultEagerness).new_task, 0.55);
  for (int level = kMinEagerness; level < kMaxEagerness; ++level) {
    EXPECT_GT(ThresholdsFor(level).confidence,
              ThresholdsFor(level + 1).confidence);
    EXPECT_GE(ThresholdsFor(level).new_task, ThresholdsFor(level + 1).new_task);
  }
  // Half is the least a probability between two answers can ask for.
  EXPECT_EQ(ThresholdsFor(kMaxEagerness).new_task, 0.5);
  EXPECT_EQ(ThresholdsFor(0).confidence,
            ThresholdsFor(kMinEagerness).confidence);
  EXPECT_EQ(ThresholdsFor(99).confidence,
            ThresholdsFor(kMaxEagerness).confidence);

  // The same answer moves a branch at one level and only suggests at another.
  const ClassificationAnswer answer = Answer("task-2", 0.75);
  EXPECT_EQ(Place(answer, {"task-1"}, ThresholdsFor(kMinEagerness)).action,
            Action::kSuggest);
  EXPECT_EQ(Place(answer, {"task-1"}, ThresholdsFor(kDefaultEagerness)).action,
            Action::kApply);
}

}  // namespace
}  // namespace uw
