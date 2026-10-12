// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/components/tab_organization/placement.h"

#include "testing/gtest/include/gtest/gtest.h"

namespace uw {
namespace {

using Action = Placement::Action;

ClassificationAnswer Answer(const std::string& choice,
                            double confidence,
                            double new_task = 0.0) {
  ClassificationAnswer answer;
  answer.choice = choice;
  answer.confidence = confidence;
  answer.new_task = new_task;
  return answer;
}

TEST(PlacementTest, ABranchMovesToAnotherTaskOnlyWhenTheModelIsSure) {
  Placement placement = Place(Answer("task-2", 0.90), "task-1", true);
  EXPECT_EQ(placement.action, Action::kApply);
  EXPECT_EQ(placement.target, "task-2");
  EXPECT_EQ(placement.confidence, 0.90);
  EXPECT_EQ(placement.needed, kMinConfidence);

  placement = Place(Answer("task-2", 0.89), std::string(), true);
  EXPECT_EQ(placement.action, Action::kSuggest);
  EXPECT_EQ(placement.target, "task-2");

  EXPECT_EQ(Place(Answer("task-1", 0.2), "task-1", true).action,
            Action::kLeave);
  EXPECT_EQ(Place(Answer("task-1", 0.99), "task-1", true).action,
            Action::kLeave);
}

TEST(PlacementTest, ABranchThatFitsNoTaskLeavesTheTaskItIsIn) {
  Placement placement =
      Place(Answer(kScratchpadOption, 0.95, 0.2), "task-1", true);
  EXPECT_EQ(placement.action, Action::kApply);
  EXPECT_EQ(placement.target, kScratchpadOption);
  EXPECT_EQ(placement.confidence, 0.95);
  EXPECT_EQ(placement.needed, kMinConfidence);

  // Leaning towards a task of its own is not enough to start one, but the
  // branch still does not belong where it is.
  placement = Place(Answer(kNewTaskOption, 0.95, 0.6), "task-1", true);
  EXPECT_EQ(placement.action, Action::kApply);
  EXPECT_EQ(placement.target, kScratchpadOption);

  placement = Place(Answer(kNewTaskOption, 0.95, 0.75), "task-1", true);
  EXPECT_EQ(placement.action, Action::kApply);
  EXPECT_EQ(placement.target, kNewTaskOption);
  EXPECT_EQ(placement.confidence, 0.75);
  EXPECT_EQ(placement.needed, kMinNewTaskProbability);

  placement = Place(Answer(kScratchpadOption, 0.89, 0.2), "task-1", true);
  EXPECT_EQ(placement.action, Action::kSuggest);
  EXPECT_EQ(placement.target, kScratchpadOption);
  EXPECT_EQ(placement.needed, kMinConfidence);
}

TEST(PlacementTest, ABranchThatMayNotLeaveIsOnlySuggested) {
  Placement placement =
      Place(Answer(kScratchpadOption, 0.99, 0.1), "task-1", false);
  EXPECT_EQ(placement.action, Action::kSuggest);
  EXPECT_EQ(placement.target, kScratchpadOption);

  placement = Place(Answer(kNewTaskOption, 0.99, 0.9), "task-1", false);
  EXPECT_EQ(placement.action, Action::kSuggest);
  EXPECT_EQ(placement.target, kNewTaskOption);

  // Joining another task is not leaving on the model's doubt about this one.
  EXPECT_EQ(Place(Answer("task-2", 0.95), "task-1", false).action,
            Action::kApply);
}

TEST(PlacementTest, InTheScratchpadOnlyAClearNewTaskChangesAnything) {
  Placement placement =
      Place(Answer(kNewTaskOption, 0.95, 0.8), std::string(), false);
  EXPECT_EQ(placement.action, Action::kApply);
  EXPECT_EQ(placement.target, kNewTaskOption);

  placement = Place(Answer(kNewTaskOption, 0.95, 0.6), std::string(), true);
  EXPECT_EQ(placement.action, Action::kSuggest);
  EXPECT_EQ(placement.target, kNewTaskOption);
  EXPECT_EQ(placement.confidence, 0.6);
  EXPECT_EQ(placement.needed, kMinNewTaskProbability);

  placement = Place(Answer(kNewTaskOption, 0.5, 0.99), std::string(), true);
  EXPECT_EQ(placement.action, Action::kSuggest);
  EXPECT_EQ(placement.confidence, 0.5);
  EXPECT_EQ(placement.needed, kMinConfidence);

  EXPECT_EQ(
      Place(Answer(kScratchpadOption, 0.99, 0.1), std::string(), true).action,
      Action::kLeave);
  EXPECT_EQ(
      Place(Answer(kScratchpadOption, 0.3, 0.4), std::string(), true).action,
      Action::kLeave);
}

}  // namespace
}  // namespace uw
