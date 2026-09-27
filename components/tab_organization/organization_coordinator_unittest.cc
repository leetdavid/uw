// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/components/tab_organization/organization_coordinator.h"

#include "base/test/task_environment.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace uw {
namespace {

class OrganizationCoordinatorTest : public testing::Test {
 protected:
  base::test::TaskEnvironment environment_;
  TabTree tree_{u"Scratchpad"};
  OrganizationCoordinator coordinator_{&tree_};
};

TEST_F(OrganizationCoordinatorTest, HighConfidenceMoveKeepsBranchHierarchy) {
  auto parent = tree_.AddPage(u"Parent");
  auto child = tree_.AddPage(u"Child");
  tree_.SetPageParent(child, parent);
  auto other = tree_.AddPage(u"Other");
  auto task = tree_.MakeTask({other}, u"Research").value();

  const uint64_t request =
      coordinator_.RequestClassification({parent});
  ASSERT_NE(request, 0u);
  OrganizationCoordinator::Result result;
  result.request_id = request;
  result.decision = OrganizationCoordinator::Decision::kMoveToTask;
  result.destination_task = task;
  result.confidence = 0.95;
  EXPECT_EQ(coordinator_.ApplyResult(result), task);
  EXPECT_EQ(tree_.Find(parent)->parent(), tree_.Find(task));
  EXPECT_EQ(tree_.Find(child)->parent(), tree_.Find(parent));
}

TEST_F(OrganizationCoordinatorTest, LowConfidenceLeavesBranchInScratchpad) {
  auto page = tree_.AddPage(u"Page");
  auto other = tree_.AddPage(u"Other");
  auto task = tree_.MakeTask({other}, u"Research").value();

  const uint64_t request =
      coordinator_.RequestClassification({page});
  OrganizationCoordinator::Result result;
  result.request_id = request;
  result.decision = OrganizationCoordinator::Decision::kMoveToTask;
  result.destination_task = task;
  result.confidence = 0.89;
  EXPECT_EQ(coordinator_.ApplyResult(result), std::nullopt);
  EXPECT_EQ(tree_.Find(page)->parent(), tree_.scratchpad());
  EXPECT_EQ(tree_.PagesIn({task}), (std::vector<TreeId>{other}));
}

TEST_F(OrganizationCoordinatorTest, NewTaskUsesTheGeneratedNameInOneUndo) {
  auto page = tree_.AddPage(u"Page");
  const uint64_t request =
      coordinator_.RequestClassification({page});
  OrganizationCoordinator::Result result;
  result.request_id = request;
  result.decision = OrganizationCoordinator::Decision::kNewTask;
  result.confidence = 0.95;
  result.task_name = u"Benchmark Jev";
  auto created = coordinator_.ApplyResult(result);
  ASSERT_TRUE(created);
  EXPECT_EQ(tree_.Find(*created)->GetTitle(), u"Benchmark Jev");
  EXPECT_EQ(tree_.Find(page)->parent(), tree_.Find(*created));

  tree_.Undo();
  EXPECT_FALSE(tree_.Find(*created));
  EXPECT_EQ(tree_.Find(page)->parent(), tree_.scratchpad());
  EXPECT_FALSE(tree_.CanUndo());
}

TEST_F(OrganizationCoordinatorTest, NewTaskWithoutANameKeepsProvisionalTitle) {
  auto page = tree_.AddPage(u"Page");
  const uint64_t request =
      coordinator_.RequestClassification({page});
  OrganizationCoordinator::Result result;
  result.request_id = request;
  result.decision = OrganizationCoordinator::Decision::kNewTask;
  result.confidence = 0.95;
  auto created = coordinator_.ApplyResult(result);
  ASSERT_TRUE(created);
  EXPECT_EQ(tree_.Find(*created)->GetTitle(), u"New task");
}

TEST_F(OrganizationCoordinatorTest, ManualMoveBlocksOutstandingResult) {
  auto page = tree_.AddPage(u"Page");
  auto other = tree_.AddPage(u"Other");
  auto first = tree_.MakeTask({other}, u"First").value();
  auto second = tree_.MakeTask({page}, u"Second").value();

  // Fresh branch classified while sitting in the second task's parent.
  tree_.Move({page}, tree_.scratchpad()->value.id);
  const uint64_t request =
      coordinator_.RequestClassification({page});
  // Manual move before the result arrives.
  tree_.Move({page}, second);
  OrganizationCoordinator::Result result;
  result.request_id = request;
  result.decision = OrganizationCoordinator::Decision::kMoveToTask;
  result.destination_task = first;
  result.confidence = 0.95;
  EXPECT_EQ(coordinator_.ApplyResult(result), std::nullopt);
  EXPECT_EQ(tree_.Find(page)->parent(), tree_.Find(second));
}

TEST_F(OrganizationCoordinatorTest, ClosedBranchIsANoOp) {
  auto page = tree_.AddPage(u"Page");
  auto other = tree_.AddPage(u"Other");
  auto task = tree_.MakeTask({other}, u"Research").value();
  const uint64_t request =
      coordinator_.RequestClassification({page});
  tree_.RemovePage(page);
  OrganizationCoordinator::Result result;
  result.request_id = request;
  result.decision = OrganizationCoordinator::Decision::kMoveToTask;
  result.destination_task = task;
  result.confidence = 0.95;
  EXPECT_EQ(coordinator_.ApplyResult(result), std::nullopt);
}

TEST_F(OrganizationCoordinatorTest, UndoPreservesNewTabs) {
  auto page = tree_.AddPage(u"Page");
  const uint64_t request =
      coordinator_.RequestClassification({page});
  OrganizationCoordinator::Result result;
  result.request_id = request;
  result.decision = OrganizationCoordinator::Decision::kNewTask;
  result.confidence = 0.95;
  auto created = coordinator_.ApplyResult(result);
  ASSERT_TRUE(created);
  auto late = tree_.AddPage(u"Late");
  tree_.Undo();
  EXPECT_FALSE(tree_.Find(*created));
  EXPECT_TRUE(tree_.Find(late));
  EXPECT_EQ(tree_.Find(page)->parent(), tree_.scratchpad());
}

TEST_F(OrganizationCoordinatorTest, InvalidDestinationIsDiscarded) {
  auto page = tree_.AddPage(u"Page");
  const uint64_t request =
      coordinator_.RequestClassification({page});
  OrganizationCoordinator::Result result;
  result.request_id = request;
  result.decision = OrganizationCoordinator::Decision::kMoveToTask;
  result.destination_task = page;
  result.confidence = 0.95;
  EXPECT_EQ(coordinator_.ApplyResult(result), std::nullopt);
  EXPECT_EQ(tree_.Find(page)->parent(), tree_.scratchpad());
}

}  // namespace
}  // namespace uw
