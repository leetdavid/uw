// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/components/tab_organization/tab_tree.h"

#include "base/test/task_environment.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace uw {
namespace {

class TabTreeTest : public testing::Test {
 protected:
  base::test::TaskEnvironment environment_;
  TabTree tree_{u"Scratchpad"};
};

TEST_F(TabTreeTest, ClosingParentPromotesChildrenWithoutDiscardingSubtrees) {
  auto parent = tree_.AddPage(u"Issue");
  auto child = tree_.AddPage(u"Patch");
  auto grandchild = tree_.AddPage(u"Discussion");
  auto sibling = tree_.AddPage(u"Reference");
  tree_.SetPageParent(child, parent);
  tree_.SetPageParent(grandchild, child);
  tree_.ToggleCollapsed(child);
  tree_.RemovePage(parent);
  EXPECT_FALSE(tree_.Find(parent));
  EXPECT_EQ(tree_.Find(child)->parent(), tree_.scratchpad());
  EXPECT_EQ(tree_.Find(grandchild)->parent(), tree_.Find(child));
  EXPECT_TRUE(tree_.Find(child)->value.collapsed);
  EXPECT_EQ(tree_.PageOrder(),
            (std::vector<TreeId>{child, grandchild, sibling}));
}

TEST_F(TabTreeTest, ExtractionNormalizesSelectionAndPreservesBranches) {
  auto a = tree_.AddPage(u"A");
  auto b = tree_.AddPage(u"B");
  auto c = tree_.AddPage(u"C");
  tree_.SetPageParent(b, a);
  auto task = tree_.MakeTask({b, a, b}, u"Investigate");
  ASSERT_TRUE(task);
  EXPECT_EQ(tree_.Find(a)->parent(), tree_.Find(*task));
  EXPECT_EQ(tree_.Find(b)->parent(), tree_.Find(a));
  EXPECT_EQ(tree_.Find(c)->parent(), tree_.scratchpad());
  EXPECT_EQ(tree_.PagesIn({*task, a, b}), (std::vector<TreeId>{a, b}));
  tree_.Undo();
  EXPECT_FALSE(tree_.Find(*task));
  EXPECT_EQ(tree_.PageOrder(), (std::vector<TreeId>{a, b, c}));
}

TEST_F(TabTreeTest, ExtractedTaskIsSiblingImmediatelyAfterSource) {
  auto a = tree_.AddPage(u"A");
  auto b = tree_.AddPage(u"B");
  auto c = tree_.AddPage(u"C");
  auto outer = tree_.MakeTask({a, b}, u"Outer").value();
  auto other = tree_.MakeTask({c}, u"Other").value();
  ASSERT_TRUE(tree_.Move({outer}, other));
  auto extracted = tree_.MakeTask({a}, u"Extracted").value();
  EXPECT_EQ(tree_.Find(extracted)->parent(), tree_.Find(other));
  const auto& children = tree_.Find(other)->children();
  ASSERT_EQ(children.size(), 3u);
  EXPECT_EQ(children[1]->value.id, outer);
  EXPECT_EQ(children[2]->value.id, extracted);
}

TEST_F(TabTreeTest, CyclesAndInvalidContainerTypesAreRejected) {
  auto a = tree_.AddPage(u"A");
  auto b = tree_.AddPage(u"B");
  tree_.SetPageParent(b, a);
  EXPECT_FALSE(tree_.Move({a}, b));
  EXPECT_FALSE(tree_.Move({a}, a));
  EXPECT_FALSE(tree_.Move({a}, tree_.root()->value.id));
  auto task = tree_.MakeTask({a}, u"Task").value();
  EXPECT_FALSE(tree_.Move({task}, b));
  EXPECT_FALSE(tree_.Move({task}, tree_.scratchpad()->value.id));
  EXPECT_FALSE(
      tree_.Move({task}, tree_.root()->value.id, tree_.scratchpad()->value.id));
  EXPECT_EQ(tree_.root()->children()[0].get(), tree_.scratchpad());
}

TEST_F(TabTreeTest, UndoKeepsNewTabsAndSkipsClosedPages) {
  auto a = tree_.AddPage(u"A");
  auto b = tree_.AddPage(u"B");
  auto task = tree_.MakeTask({a, b}, u"Task").value();
  auto new_tab = tree_.AddPage(u"New tab");
  auto new_child = tree_.AddPage(u"New child");
  tree_.SetPageParent(new_child, a);
  tree_.RemovePage(b);
  tree_.Undo();
  EXPECT_FALSE(tree_.Find(task));
  EXPECT_FALSE(tree_.Find(b));
  EXPECT_TRUE(tree_.Find(new_tab));
  EXPECT_EQ(tree_.Find(new_child)->parent(), tree_.Find(a));
  EXPECT_EQ(tree_.Find(a)->parent(), tree_.scratchpad());
}

TEST_F(TabTreeTest, UndoUsesSurvivingAncestorWhenOriginalParentClosed) {
  auto parent = tree_.AddPage(u"Parent");
  auto child = tree_.AddPage(u"Child");
  tree_.SetPageParent(child, parent);
  tree_.MakeTask({child}, u"Task");
  tree_.RemovePage(parent);
  tree_.Undo();
  EXPECT_EQ(tree_.Find(child)->parent(), tree_.scratchpad());
}

TEST_F(TabTreeTest, CollapseChoicesSurviveMovesAndUndo) {
  auto a = tree_.AddPage(u"A");
  auto b = tree_.AddPage(u"B");
  tree_.SetPageParent(b, a);
  tree_.ToggleCollapsed(a);
  tree_.MakeTask({a}, u"Task");
  EXPECT_TRUE(tree_.Find(a)->value.collapsed);
  tree_.Undo();
  EXPECT_TRUE(tree_.Find(a)->value.collapsed);
}

TEST_F(TabTreeTest, RenameIsAnIndependentUndoableAction) {
  auto a = tree_.AddPage(u"A");
  auto task = tree_.MakeTask({a}, u"Initial").value();
  tree_.RenameTask(task, u"Manual name");
  tree_.Undo();
  EXPECT_EQ(tree_.Find(task)->GetTitle(), u"Initial");
  tree_.Undo();
  EXPECT_FALSE(tree_.Find(task));
  EXPECT_TRUE(tree_.Find(a));
}

TEST_F(TabTreeTest, UndoingRenameThenCreationReturnsPageToScratchpad) {
  auto page = tree_.AddPage(u"Page");
  auto task = tree_.MakeTask({page}, u"Generated").value();
  tree_.RenameTask(task, u"Manual");
  tree_.Undo();
  tree_.Undo();
  EXPECT_FALSE(tree_.Find(task));
  EXPECT_EQ(tree_.Find(page)->parent(), tree_.scratchpad());
}

TEST_F(TabTreeTest, SessionPathsRestoreNestedTasksAndLateParent) {
  auto parent = tree_.AddPage(u"Parent");
  auto child = tree_.AddPage(u"Child");
  tree_.SetPageParent(child, parent);
  auto task = tree_.MakeTask({parent}, u"Task").value();
  tree_.ToggleCollapsed(task);
  tree_.ToggleCollapsed(parent);
  const auto parent_state = tree_.SavePage(parent);
  const auto child_state = tree_.SavePage(child);

  TabTree restored(u"Scratchpad");
  EXPECT_EQ(restored.RestorePage(child_state, u"Child"), child);
  restored.ResolveRestoredParents();
  EXPECT_EQ(restored.Find(child)->parent(), restored.Find(task));
  EXPECT_EQ(restored.RestorePage(parent_state, u"Parent"), parent);
  restored.ResolveRestoredParents();
  EXPECT_EQ(restored.Find(child)->parent(), restored.Find(parent));
  EXPECT_TRUE(restored.Find(task)->value.collapsed);
  EXPECT_TRUE(restored.Find(parent)->value.collapsed);
}

TEST_F(TabTreeTest, LateRestoreCannotOverrideManualPlacement) {
  auto parent = tree_.AddPage(u"Parent");
  auto child = tree_.AddPage(u"Child");
  tree_.SetPageParent(child, parent);
  auto parent_state = tree_.SavePage(parent);
  auto child_state = tree_.SavePage(child);
  TabTree restored(u"Scratchpad");
  restored.RestorePage(child_state, u"Child");
  auto manual = restored.MakeTask({child}, u"Manual").value();
  restored.RestorePage(parent_state, u"Parent");
  restored.ResolveRestoredParents();
  EXPECT_EQ(restored.Find(child)->parent(), restored.Find(manual));
}

TEST_F(TabTreeTest, InvalidRestorationDoesNotCreatePhantomTasks) {
  auto page = tree_.AddPage(u"Page");
  auto task = tree_.MakeTask({page}, u"Task").value();
  auto state = tree_.SavePage(page);
  state.FindList("tasks")->Append(state.FindList("tasks")->front().Clone());
  TabTree restored(u"Scratchpad");
  auto restored_page = restored.RestorePage(state, u"Page");
  EXPECT_FALSE(restored.Find(task));
  EXPECT_EQ(restored.Find(restored_page)->parent(), restored.scratchpad());
}

TEST_F(TabTreeTest, PlaceBesideInsertsAfterSiblingWithoutUndo) {
  auto a = tree_.AddPage(u"A");
  auto b = tree_.AddPage(u"B");
  auto c = tree_.AddPage(u"C");
  EXPECT_TRUE(tree_.PlaceBeside(c, a));
  EXPECT_EQ(tree_.PageOrder(), (std::vector<TreeId>{a, c, b}));
  EXPECT_EQ(tree_.Find(c)->parent(), tree_.scratchpad());
  // Structural placement: no undo entry, no manual mark.
  EXPECT_FALSE(tree_.CanUndo());
  EXPECT_EQ(tree_.ManualVersion(c), 0u);
}

TEST_F(TabTreeTest, PlaceBesideRejectsInvalidPlacements) {
  auto a = tree_.AddPage(u"A");
  EXPECT_FALSE(tree_.PlaceBeside(a, a));
  EXPECT_FALSE(tree_.PlaceBeside(TreeId("missing"), a));
  EXPECT_FALSE(tree_.PlaceBeside(a, TreeId("missing")));
  auto task = tree_.MakeTask({a}, u"Task").value();
  EXPECT_FALSE(tree_.PlaceBeside(a, task));
}

TEST_F(TabTreeTest, DropTargetInsertsBeforeAndAfterAtAnchorLevel) {
  auto a = tree_.AddPage(u"A");
  auto b = tree_.AddPage(u"B");
  auto before = tree_.DropTargetForAnchor(b, 0, /*after=*/false);
  ASSERT_TRUE(before);
  EXPECT_EQ(before->parent, tree_.scratchpad()->value.id);
  EXPECT_EQ(before->before, b);
  auto after = tree_.DropTargetForAnchor(a, 0, /*after=*/true);
  ASSERT_TRUE(after);
  EXPECT_EQ(after->parent, tree_.scratchpad()->value.id);
  EXPECT_EQ(after->before, b);
}

TEST_F(TabTreeTest, DropTargetNestsAndOutdentsByLevel) {
  auto a = tree_.AddPage(u"A");
  auto b = tree_.AddPage(u"B");
  auto task = tree_.MakeTask({a}, u"Task").value();
  // Nest under the task.
  auto nest = tree_.DropTargetForAnchor(task, 1, /*after=*/true);
  ASSERT_TRUE(nest);
  EXPECT_EQ(nest->parent, task);
  EXPECT_FALSE(nest->before);
  // Outdenting a nested page past its task reaches the root, which page
  // callers map onto Scratchpad. Tasks may use the root directly.
  auto out = tree_.DropTargetForAnchor(a, -1, /*after=*/true);
  ASSERT_TRUE(out);
  EXPECT_EQ(out->parent, tree_.root()->value.id);
  EXPECT_FALSE(out->before);
  auto task_out = tree_.DropTargetForAnchor(task, -1, /*after=*/true);
  ASSERT_TRUE(task_out);
  EXPECT_EQ(task_out->parent, tree_.root()->value.id);
  // Outdenting past the top stays at the top level.
  auto clamped = tree_.DropTargetForAnchor(b, -3, /*after=*/false);
  ASSERT_TRUE(clamped);
  EXPECT_EQ(clamped->parent, tree_.scratchpad()->value.id);
  // Unknown anchors and section headers have no structural target.
  EXPECT_FALSE(
      tree_.DropTargetForAnchor(TreeId("missing"), 0, /*after=*/true));
  EXPECT_FALSE(tree_.DropTargetForAnchor(tree_.scratchpad()->value.id, 0,
                                         /*after=*/true));
  (void)task;
}

}  // namespace
}  // namespace uw
