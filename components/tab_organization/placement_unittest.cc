// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/components/tab_organization/placement.h"

#include <map>
#include <string>
#include <vector>

#include "testing/gtest/include/gtest/gtest.h"

namespace uw {
namespace {

using Action = Placement::Action;

constexpr Thresholds kNeeded{0.90, 0.75, 0.90};

ClassificationAnswer Answer(const std::string& choice,
                            double confidence,
                            double new_task = 0.0,
                            double same_work = 0.0) {
  ClassificationAnswer answer;
  answer.choice = choice;
  answer.confidence = confidence;
  answer.new_task = new_task;
  answer.same_work = same_work;
  return answer;
}

// The answer about a page the model calls a hub, such as a feed.
ClassificationAnswer Hub(ClassificationAnswer answer) {
  answer.hub = 0.9;
  return answer;
}

Placement PlaceIn(const std::string& task, const ClassificationAnswer& answer) {
  return Place(answer, {task, /*in_unnamed_task=*/false}, kNeeded);
}

PageId Id(const std::string& id) {
  return PageId(id);
}

std::vector<PageId> Ids(const std::vector<std::string>& ids) {
  std::vector<PageId> pages;
  for (const std::string& id : ids) {
    pages.push_back(Id(id));
  }
  return pages;
}

// The Warp task of the plan: two pages about Warp, a search for slippers, and
// two results opened from the search, one of which has a tab under it.
PageTree WarpTask() {
  return PageTree({{Id("warp"), "task-1", PageId()},
                   {Id("ssh"), "task-1", PageId()},
                   {Id("search"), "task-1", PageId()},
                   {Id("result"), "task-1", Id("search")},
                   {Id("steps"), "task-1", Id("result")},
                   {Id("other"), "task-1", Id("search")}});
}

std::vector<BranchPlacement> PlaceSearch(
    const std::map<PageId, TabAnswer>& tabs,
    const PageTree& tree = WarpTask()) {
  return PlaceBranch(tree, Id("search"), tabs, {"task-1"}, kNeeded);
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

TEST(PlacementTest, InTheScratchpadOnlyAClearNewTaskChangesAnything) {
  Placement placement =
      PlaceIn(std::string(), Answer(kNewTaskOption, 0.95, 0.8));
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
  // The branch fits no task name, and is the same piece of work as the other
  // tabs of its task. Without the name check it would start a task.
  Placement placement =
      PlaceIn("task-1", Answer(kNewTaskOption, 0.95, 0.9, /*same_work=*/0.90));
  EXPECT_EQ(placement.action, Action::kName);
  EXPECT_EQ(placement.confidence, 0.90);
  EXPECT_EQ(placement.needed, kNeeded.new_name);
  EXPECT_EQ(PlaceIn("task-1", Answer(kNewTaskOption, 0.5, 0.9, 0.95)).action,
            Action::kName);

  // A branch that fits its task by name says nothing against the name.
  EXPECT_EQ(PlaceIn("task-1", Answer("task-1", 0.95, 0.0, 0.99)).action,
            Action::kLeave);

  // Short of the threshold, the task keeps its name. The branch is still the
  // same piece of work, so it stays, and the new task is only suggested.
  placement = PlaceIn("task-1", Answer(kNewTaskOption, 0.95, 0.9, 0.89));
  EXPECT_EQ(placement.action, Action::kSuggest);
  EXPECT_EQ(placement.target, kNewTaskOption);

  // What the model calls a one-off renames nothing.
  placement = PlaceIn("task-1", Answer(kScratchpadOption, 0.95, 0.1, 0.99));
  EXPECT_EQ(placement.action, Action::kSuggest);
  EXPECT_EQ(placement.target, kScratchpadOption);
  // Anything else can rename the task, even short of starting one itself.
  EXPECT_EQ(PlaceIn("task-1", Answer(kNewTaskOption, 0.95, 0.74, 0.99)).action,
            Action::kName);

  // Another task the branch belongs to comes first, and renames nothing.
  placement = PlaceIn("task-1", Answer("task-2", 0.95, 0.0, 0.99));
  EXPECT_EQ(placement.action, Action::kSuggest);
  EXPECT_EQ(placement.target, "task-2");

  // A name the user typed is never replaced.
  placement = Place(Answer(kNewTaskOption, 0.95, 0.9, 0.95),
                    {"task-1", /*in_unnamed_task=*/false, /*typed_name=*/true},
                    kNeeded);
  EXPECT_EQ(placement.action, Action::kSuggest);
  EXPECT_EQ(placement.target, kNewTaskOption);

  // The Scratchpad has no name to outgrow.
  EXPECT_EQ(
      PlaceIn(std::string(), Answer(kNewTaskOption, 0.95, 0.9, 0.99)).action,
      Action::kApply);
}

TEST(PlacementTest, ABranchLeavesItsTaskOnlyAsAnotherPieceOfWork) {
  // Sure that the branch belongs elsewhere, the model must also find it a
  // different piece of work from the other tabs of its task.
  Placement placement = PlaceIn("task-1", Answer("task-2", 0.95, 0.0, 0.05));
  EXPECT_EQ(placement.action, Action::kApply);
  EXPECT_EQ(placement.target, "task-2");
  placement = PlaceIn("task-1", Answer("task-2", 0.95, 0.0, 0.2));
  EXPECT_EQ(placement.action, Action::kSuggest);
  EXPECT_EQ(placement.target, "task-2");
  EXPECT_DOUBLE_EQ(placement.confidence, 0.8);
  EXPECT_EQ(placement.needed, kNeeded.confidence);

  // The same holds for a new task and for the Scratchpad.
  EXPECT_EQ(PlaceIn("task-1", Answer(kNewTaskOption, 0.95, 0.9, 0.2)).action,
            Action::kSuggest);
  EXPECT_EQ(PlaceIn("task-1", Answer(kScratchpadOption, 0.95, 0.1, 0.2)).action,
            Action::kSuggest);
  EXPECT_EQ(
      PlaceIn("task-1", Answer(kScratchpadOption, 0.95, 0.1, 0.05)).action,
      Action::kApply);

  // A task with no other tabs has no name check, so nothing holds the
  // branch.
  EXPECT_EQ(PlaceIn("task-1", Answer(kScratchpadOption, 0.95, 0.1)).action,
            Action::kApply);

  // A branch in a task without a name joins a named task the same way.
  EXPECT_EQ(Place(Answer("task-2", 0.95, 0.0, 0.2),
                  {std::string(), /*in_unnamed_task=*/true}, kNeeded)
                .action,
            Action::kSuggest);

  // The Scratchpad holds nothing.
  EXPECT_EQ(PlaceIn(std::string(), Answer("task-2", 0.95, 0.0, 0.99)).action,
            Action::kApply);
}

TEST(PlacementTest, EachThresholdDecidesOneKindOfAnswer) {
  const ClassificationAnswer joins = Answer("task-2", 0.75);
  EXPECT_EQ(Place(joins, {"task-1"}, {0.80, 0.50, 0.50}).action,
            Action::kSuggest);
  EXPECT_EQ(Place(joins, {"task-1"}, {0.75, 0.95, 0.95}).action,
            Action::kApply);

  const ClassificationAnswer starts = Answer(kNewTaskOption, 0.99, 0.6);
  EXPECT_EQ(Place(starts, {}, {0.50, 0.65, 0.50}).action, Action::kSuggest);
  EXPECT_EQ(Place(starts, {}, {0.95, 0.60, 0.95}).action, Action::kApply);

  const ClassificationAnswer outgrown = Answer(kNewTaskOption, 0.99, 0.6, 0.6);
  EXPECT_EQ(Place(outgrown, {"task-1"}, {0.50, 0.50, 0.65}).action,
            Action::kSuggest);
  // The move threshold also says how sure the name check must be that the
  // branch is another piece of work.
  const ClassificationAnswer other = Answer(kNewTaskOption, 0.99, 0.6, 0.45);
  EXPECT_EQ(Place(other, {"task-1"}, {0.55, 0.50, 0.65}).action,
            Action::kApply);
  EXPECT_EQ(Place(other, {"task-1"}, {0.60, 0.50, 0.65}).action,
            Action::kSuggest);
  // The other two thresholds have no say in a new name.
  const Placement placement = Place(outgrown, {"task-1"}, {0.95, 0.95, 0.60});
  EXPECT_EQ(placement.action, Action::kName);
  EXPECT_EQ(placement.needed, 0.60);
  EXPECT_EQ(Place(Answer(kNewTaskOption, 0.6, 0.6, 0.6), {"task-1"},
                  {0.95, 0.95, 0.60})
                .action,
            Action::kName);
}

TEST(PlacementTest, AHubIsNeverMadeIntoATask) {
  // Whatever the model makes of it otherwise, it starts no task.
  const ClassificationAnswer feed = Hub(Answer(kNewTaskOption, 0.99, 0.99));
  EXPECT_EQ(PlaceIn(std::string(), feed).action, Action::kLeave);

  // It gives no task a name, a first one or a new one.
  EXPECT_EQ(
      Place(feed, {std::string(), /*in_unnamed_task=*/true}, kNeeded).action,
      Action::kLeave);
  EXPECT_EQ(
      PlaceIn("task-1", Hub(Answer(kNewTaskOption, 0.99, 0.99, 0.99))).action,
      Action::kLeave);

  // A hub in the Scratchpad that fits a task by name still joins it.
  const Placement placement =
      PlaceIn(std::string(), Hub(Answer("task-2", 0.95)));
  EXPECT_EQ(placement.action, Action::kApply);
  EXPECT_EQ(placement.target, "task-2");

  // Short of even odds, the page is not a hub.
  ClassificationAnswer page = Answer(kNewTaskOption, 0.99, 0.99);
  page.hub = 0.49;
  EXPECT_EQ(PlaceIn(std::string(), page).target, kNewTaskOption);
}

// A chat assistant's home page opened in a task fits no task by name. It stays
// there, whatever else the model says about it, and suggests nothing.
TEST(PlacementTest, AHubInATaskStaysThere) {
  for (const ClassificationAnswer& answer :
       {Hub(Answer(kScratchpadOption, 0.99)),
        Hub(Answer(kNewTaskOption, 0.99, 0.99)),
        Hub(Answer(kNewTaskOption, 0.5, 0.99)), Hub(Answer("task-2", 0.99))}) {
    const Placement placement = PlaceIn("task-1", answer);
    EXPECT_EQ(placement.action, Action::kLeave);
    EXPECT_EQ(placement.target, "task-1");
    EXPECT_EQ(Place(answer, {std::string(), /*in_unnamed_task=*/true}, kNeeded)
                  .action,
              Action::kLeave);
  }
  const PageTree tree({{Id("chat"), "task-1", PageId()}});
  const std::vector<BranchPlacement> placements = PlaceBranch(
      tree, Id("chat"), {{Id("chat"), {Hub(Answer(kScratchpadOption, 0.99))}}},
      {"task-1", /*in_unnamed_task=*/false}, kNeeded);
  ASSERT_EQ(placements.size(), 1u);
  EXPECT_EQ(placements[0].placement.action, Action::kLeave);
}

// A feed in the Scratchpad and an article opened from it. The article starts
// a task. The feed stays where it is.
TEST(PlacementTest, TabsUnderAHubLeaveWithoutIt) {
  const PageTree tree({{Id("feed"), std::string(), PageId()},
                       {Id("article"), std::string(), Id("feed")},
                       {Id("comments"), std::string(), Id("article")}});
  std::vector<BranchPlacement> placements =
      PlaceBranch(tree, Id("feed"),
                  {{Id("feed"), {Hub(Answer(kScratchpadOption, 0.99, 0.1))}},
                   {Id("article"), {Answer(kNewTaskOption, 0.99, 0.9)}}},
                  {}, kNeeded);
  ASSERT_EQ(placements.size(), 2u);
  EXPECT_EQ(placements[0].top, Id("feed"));
  EXPECT_EQ(placements[0].pages, Ids({"feed"}));
  EXPECT_EQ(placements[0].placement.action, Action::kLeave);
  EXPECT_EQ(placements[1].top, Id("article"));
  EXPECT_EQ(placements[1].pages, Ids({"article", "comments"}));
  EXPECT_EQ(placements[1].placement.action, Action::kApply);
  EXPECT_EQ(placements[1].placement.target, kNewTaskOption);

  // A search page is no hub, so it still goes with its result.
  placements =
      PlaceBranch(tree, Id("feed"),
                  {{Id("feed"), {Answer(kScratchpadOption, 0.99, 0.1)}},
                   {Id("article"), {Answer(kNewTaskOption, 0.99, 0.9)}}},
                  {}, kNeeded);
  ASSERT_EQ(placements.size(), 1u);
  EXPECT_EQ(placements[0].pages, Ids({"feed", "article", "comments"}));
  EXPECT_EQ(placements[0].placement.target, kNewTaskOption);
}

TEST(PlacementTest, AHubNeverGoesAlongWithThePageAboveIt) {
  // The search leaves for a task of its own. The hubs under it stay in the
  // task, sure of their answer or not.
  std::vector<BranchPlacement> placements =
      PlaceSearch({{Id("search"), {Answer(kNewTaskOption, 0.99, 0.9)}},
                   {Id("result"), {Hub(Answer(kNewTaskOption, 0.5, 0.9))}},
                   {Id("other"), {Hub(Answer(kNewTaskOption, 0.99, 0.9))}}});
  ASSERT_EQ(placements.size(), 3u);
  EXPECT_EQ(placements[0].top, Id("search"));
  EXPECT_EQ(placements[0].pages, Ids({"search"}));
  EXPECT_EQ(placements[0].staying, Ids({"result", "other"}));
  EXPECT_EQ(placements[0].placement.target, kNewTaskOption);
  EXPECT_EQ(placements[1].top, Id("result"));
  EXPECT_EQ(placements[1].placement.action, Action::kLeave);
  EXPECT_EQ(placements[2].top, Id("other"));
  EXPECT_EQ(placements[2].placement.action, Action::kLeave);

  // Its own score cannot make the tabs around it a task either.
  placements =
      PlaceSearch({{Id("search"), {Answer(kScratchpadOption, 0.99, 0.2)}},
                   {Id("result"), {Hub(Answer(kNewTaskOption, 0.5, 0.99))}}});
  ASSERT_FALSE(placements.empty());
  EXPECT_EQ(placements[0].placement.target, kScratchpadOption);
}

// The case from the user's log: a search that has nothing to do with its
// task, and the result opened from it.
TEST(PlacementTest, TabsUnderEachOtherThatAgreeLeaveTogether) {
  const std::vector<BranchPlacement> placements =
      PlaceSearch({{Id("search"), {Answer(kNewTaskOption, 0.99, 0.8)}},
                   {Id("result"), {Answer(kNewTaskOption, 0.99, 0.9)}}});
  ASSERT_EQ(placements.size(), 1u);
  EXPECT_EQ(placements[0].top, Id("search"));
  // Tabs that were not asked about go with the page above them.
  EXPECT_EQ(placements[0].pages, Ids({"search", "result", "steps", "other"}));
  EXPECT_TRUE(placements[0].staying.empty());
  EXPECT_EQ(placements[0].placement.action, Action::kApply);
  EXPECT_EQ(placements[0].placement.target, kNewTaskOption);
}

TEST(PlacementTest, AnyTabOfAGroupCanMakeItTheStartOfATask) {
  // Alone, the search is a one-off for the Scratchpad.
  std::vector<BranchPlacement> placements =
      PlaceSearch({{Id("search"), {Answer(kScratchpadOption, 0.99, 0.2)}}});
  ASSERT_EQ(placements.size(), 1u);
  EXPECT_EQ(placements[0].placement.target, kScratchpadOption);

  placements =
      PlaceSearch({{Id("search"), {Answer(kScratchpadOption, 0.99, 0.2)}},
                   {Id("steps"), {Answer(kNewTaskOption, 0.99, 0.9)}}});
  ASSERT_EQ(placements.size(), 1u);
  EXPECT_EQ(placements[0].top, Id("search"));
  EXPECT_EQ(placements[0].placement.action, Action::kApply);
  EXPECT_EQ(placements[0].placement.target, kNewTaskOption);
  EXPECT_EQ(placements[0].placement.confidence, 0.9);
  EXPECT_EQ(placements[0].placement.needed, kNeeded.new_task);

  // A tab the model is not sure about still counts, once it goes along.
  placements =
      PlaceSearch({{Id("search"), {Answer(kScratchpadOption, 0.99, 0.2)}},
                   {Id("result"), {Answer(kNewTaskOption, 0.5, 0.9)}}});
  ASSERT_EQ(placements.size(), 1u);
  EXPECT_EQ(placements[0].placement.target, kNewTaskOption);
}

TEST(PlacementTest, ATabThatFitsItsTaskStaysBehind) {
  const std::vector<BranchPlacement> placements =
      PlaceSearch({{Id("search"), {Answer(kNewTaskOption, 0.99, 0.8)}},
                   {Id("result"), {Answer("task-1", 0.4)}},
                   {Id("other"), {std::nullopt, /*placed=*/true}}});
  ASSERT_EQ(placements.size(), 2u);
  EXPECT_EQ(placements[0].top, Id("search"));
  // The tab under the result was not asked about. It stays with the result.
  EXPECT_EQ(placements[0].pages, Ids({"search"}));
  EXPECT_EQ(placements[0].staying, Ids({"result", "other"}));
  EXPECT_EQ(placements[0].placement.action, Action::kApply);
  EXPECT_EQ(placements[1].top, Id("result"));
  EXPECT_EQ(placements[1].placement.action, Action::kLeave);
}

TEST(PlacementTest, AChildNestedByHandGoesWithThePageAboveIt) {
  std::vector<BranchPlacement> placements =
      PlaceSearch({{Id("search"), {Answer(kNewTaskOption, 0.99, 0.8)}},
                   {Id("result"), {Answer("task-1", 0.4)}},
                   {Id("other"),
                    {std::nullopt, /*placed=*/true,
                     /*held=*/true}}});
  ASSERT_EQ(placements.size(), 2u);
  EXPECT_EQ(placements[0].pages, Ids({"search", "other"}));
  EXPECT_EQ(placements[0].staying, Ids({"result"}));

  // Under a page that stays, it stays too.
  placements = PlaceSearch({{Id("search"), {Answer("task-1", 0.95)}},
                            {Id("other"),
                             {std::nullopt, /*placed=*/true,
                              /*held=*/true}}});
  ASSERT_EQ(placements.size(), 1u);
  EXPECT_EQ(placements[0].pages, Ids({"search"}));
  EXPECT_EQ(placements[0].placement.action, Action::kLeave);
}

TEST(PlacementTest, ATabThatBelongsElsewhereLeavesOnItsOwn) {
  std::vector<BranchPlacement> placements =
      PlaceSearch({{Id("search"), {Answer(kNewTaskOption, 0.99, 0.8)}},
                   {Id("result"), {Answer("task-2", 0.95)}}});
  ASSERT_EQ(placements.size(), 2u);
  EXPECT_EQ(placements[0].pages, Ids({"search", "other"}));
  EXPECT_EQ(placements[0].staying, Ids({"result"}));
  EXPECT_EQ(placements[1].top, Id("result"));
  EXPECT_EQ(placements[1].pages, Ids({"result", "steps"}));
  EXPECT_EQ(placements[1].placement.action, Action::kApply);
  EXPECT_EQ(placements[1].placement.target, "task-2");

  // Under a page that stays, a tab leaves without it.
  placements =
      PlaceSearch({{Id("search"), {Answer("task-1", 0.95)}},
                   {Id("result"), {Answer(kNewTaskOption, 0.99, 0.9)}}});
  ASSERT_EQ(placements.size(), 2u);
  EXPECT_EQ(placements[0].placement.action, Action::kLeave);
  EXPECT_EQ(placements[1].pages, Ids({"result", "steps"}));
  EXPECT_EQ(placements[1].placement.target, kNewTaskOption);
}

TEST(PlacementTest, AnAnswerShortOfTheThresholdMovesNothingByItself) {
  const std::vector<BranchPlacement> placements =
      PlaceSearch({{Id("search"), {Answer(kNewTaskOption, 0.89, 0.9)}},
                   {Id("result"), {Answer("task-2", 0.5)}}});
  ASSERT_EQ(placements.size(), 2u);
  EXPECT_EQ(placements[0].pages, Ids({"search"}));
  EXPECT_EQ(placements[0].placement.action, Action::kSuggest);
  EXPECT_EQ(placements[1].pages, Ids({"result"}));
  EXPECT_EQ(placements[1].placement.action, Action::kSuggest);
  EXPECT_EQ(placements[1].placement.target, "task-2");
}

TEST(PlacementTest, TheNameCheckHoldsATabInItsTaskUnlessItGoesAlong) {
  // The search is the same piece of work as the other tabs of its task, so
  // it stays, and the task has outgrown its name. The result under it is
  // another piece of work and leaves with the tab under it.
  std::vector<BranchPlacement> placements =
      PlaceSearch({{Id("search"), {Answer(kNewTaskOption, 0.99, 0.8, 0.95)}},
                   {Id("result"), {Answer(kNewTaskOption, 0.99, 0.9, 0.05)}}});
  ASSERT_EQ(placements.size(), 2u);
  EXPECT_EQ(placements[0].pages, Ids({"search"}));
  EXPECT_EQ(placements[0].placement.action, Action::kName);
  EXPECT_EQ(placements[1].pages, Ids({"result", "steps"}));
  EXPECT_EQ(placements[1].placement.action, Action::kApply);
  EXPECT_EQ(placements[1].placement.target, kNewTaskOption);

  // A tab held in its task that would go elsewhere stays behind when the
  // page above it leaves.
  placements =
      PlaceSearch({{Id("search"), {Answer(kNewTaskOption, 0.99, 0.8, 0.05)}},
                   {Id("result"), {Answer("task-2", 0.99, 0.0, 0.95)}}});
  ASSERT_EQ(placements.size(), 2u);
  EXPECT_EQ(placements[0].pages, Ids({"search", "other"}));
  EXPECT_EQ(placements[0].staying, Ids({"result"}));
  EXPECT_EQ(placements[0].placement.action, Action::kApply);
  EXPECT_EQ(placements[1].pages, Ids({"result"}));
  EXPECT_EQ(placements[1].placement.action, Action::kSuggest);

  // A tab below a leaving page that agrees with it goes along, whatever the
  // name check says about it: the tabs it was compared with may be the very
  // ones that leave.
  placements =
      PlaceSearch({{Id("search"), {Answer(kNewTaskOption, 0.99, 0.8, 0.05)}},
                   {Id("result"), {Answer(kNewTaskOption, 0.99, 0.9, 0.95)}}});
  ASSERT_EQ(placements.size(), 1u);
  EXPECT_EQ(placements[0].placement.action, Action::kApply);
  EXPECT_EQ(placements[0].placement.target, kNewTaskOption);
}

// Carries out every move of `placements` the way the window does, and marks
// what moved as placed.
void Apply(const std::vector<BranchPlacement>& placements,
           PageTree& tree,
           std::map<PageId, TabAnswer>& tabs) {
  int next_task = 0;
  for (const BranchPlacement& group : placements) {
    if (group.placement.action != Action::kApply) {
      continue;
    }
    for (const PageId& staying : group.staying) {
      while (tree.DepthOf(staying) > tree.DepthOf(group.top)) {
        tree.Outdent(staying);
      }
    }
    if (group.placement.target == kNewTaskOption) {
      tree.MoveToNewTask(group.top, "new-" + std::to_string(next_task++));
    } else {
      tree.MoveToTask(group.top, group.placement.target == kScratchpadOption
                                     ? std::string()
                                     : group.placement.target);
    }
    for (const PageId& moved : tree.BranchOf(group.top)) {
      tabs[moved] = {std::nullopt, /*placed=*/true};
    }
  }
}

TEST(PlacementTest, TheSameAnswersGiveTheSameMovesAndASecondPassMovesNothing) {
  PageTree tree({{Id("warp"), "task-1", PageId()},
                 {Id("ssh"), "task-1", PageId()},
                 {Id("search"), "task-1", PageId()},
                 {Id("result"), "task-1", Id("search")},
                 {Id("steps"), "task-1", Id("result")},
                 {Id("other"), "task-1", Id("search")},
                 {Id("lisbon"), "task-2", PageId()}});
  std::map<PageId, TabAnswer> tabs = {
      {Id("warp"), {Answer("task-1", 0.95)}},
      {Id("search"), {Answer(kNewTaskOption, 0.99, 0.8)}},
      {Id("result"), {Answer("task-2", 0.95)}},
      {Id("steps"), {Answer("task-1", 0.3)}},
      {Id("other"), {Answer(kScratchpadOption, 0.99, 0.1)}}};
  const std::vector<BranchPlacement> first = PlaceSearch(tabs, tree);
  EXPECT_EQ(PlaceSearch(tabs, tree), first);
  ASSERT_EQ(first.size(), 3u);

  Apply(first, tree, tabs);
  const Page* search = tree.Find(Id("search"));
  const Page* other = tree.Find(Id("other"));
  EXPECT_EQ(search->task, "new-0");
  EXPECT_EQ(other->task, "new-0");
  EXPECT_EQ(other->parent, Id("search"));
  EXPECT_EQ(tree.Find(Id("result"))->task, "task-2");
  // The tab that fits the old task stays there, at the top level.
  EXPECT_EQ(tree.Find(Id("steps"))->task, "task-1");
  EXPECT_EQ(tree.DepthOf(Id("steps")), 0);

  const PageTree after = tree;
  for (const Page& page : after.pages()) {
    if (page.parent.value().empty()) {
      Apply(PlaceBranch(tree, page.id, tabs, {page.task}, kNeeded), tree, tabs);
    }
  }
  EXPECT_EQ(tree, after);
}

TEST(PlacementTest, AThresholdSettingStaysInItsRange) {
  EXPECT_EQ(kDefaultThresholds.confidence, 0.70);
  EXPECT_EQ(kDefaultThresholds.new_task, 0.55);
  EXPECT_EQ(kDefaultThresholds.new_name, 0.70);
  EXPECT_EQ(AsThreshold(0.2), kMinThreshold);
  EXPECT_EQ(AsThreshold(1.0), kMaxThreshold);
  // A slider stop that arithmetic left a hair off is the stop itself.
  EXPECT_EQ(AsThreshold(0.5 + 4 * 0.05), 0.70);
  EXPECT_EQ(AsThreshold(0.5499999), 0.55);
}

TidyTarget ToTask(const std::string& task) {
  return {task, u""};
}

TidyTarget ToNewTask(const std::u16string& name) {
  return {"", name};
}

// A Scratchpad with a feed and two stories opened from it, a page about
// grinders with a review under it, and a lone page.
PageTree LooseTabs() {
  return PageTree({{Id("feed"), "", PageId()},
                   {Id("story"), "", Id("feed")},
                   {Id("talk"), "", Id("feed")},
                   {Id("grinders"), "", PageId()},
                   {Id("review"), "", Id("grinders")},
                   {Id("lone"), "", PageId()}});
}

TEST(PlanTidyTest, EachTabGoesWhereItsAnswerPutsIt) {
  const std::vector<TidyMove> moves =
      PlanTidy(LooseTabs(),
               {{Id("feed"), TidyTarget()},
                {Id("story"), ToTask("task-1")},
                {Id("talk"), ToNewTask(u"Choose a grinder")},
                {Id("grinders"), ToNewTask(u"Choose a grinder")},
                {Id("review"), ToNewTask(u"Choose a grinder")},
                {Id("lone"), TidyTarget()}},
               {});
  // The feed stays, and its stories leave it. The review goes along under
  // its page.
  EXPECT_EQ(moves, (std::vector<TidyMove>{
                       {Id("story"), {}, ToTask("task-1")},
                       {Id("talk"), {}, ToNewTask(u"Choose a grinder")},
                       {Id("grinders"), {}, ToNewTask(u"Choose a grinder")}}));
}

TEST(PlanTidyTest, AChildPlacedElsewhereLeavesItsPage) {
  const std::vector<TidyMove> moves =
      PlanTidy(LooseTabs(),
               {{Id("feed"), ToTask("task-1")},
                {Id("story"), ToTask("task-1")},
                {Id("talk"), ToTask("task-2")},
                {Id("grinders"), ToTask("task-2")},
                {Id("review"), TidyTarget()}},
               {});
  EXPECT_EQ(moves, (std::vector<TidyMove>{
                       {Id("feed"), Ids({"talk"}), ToTask("task-1")},
                       {Id("talk"), {}, ToTask("task-2")},
                       {Id("grinders"), Ids({"review"}), ToTask("task-2")}}));
}

TEST(PlanTidyTest, AChildNestedByHandFollowsItsPage) {
  const std::vector<TidyMove> moves = PlanTidy(
      LooseTabs(),
      {{Id("grinders"), ToTask("task-1")}, {Id("review"), TidyTarget()}},
      {Id("review")});
  EXPECT_EQ(moves,
            (std::vector<TidyMove>{{Id("grinders"), {}, ToTask("task-1")}}));
}

// A kept tab is not asked about, so it stays, even under a page that moves.
TEST(PlanTidyTest, ATabNotAskedAboutStays) {
  const std::vector<TidyMove> moves =
      PlanTidy(LooseTabs(), {{Id("grinders"), ToTask("task-1")}}, {});
  EXPECT_EQ(moves, (std::vector<TidyMove>{
                       {Id("grinders"), Ids({"review"}), ToTask("task-1")}}));
}

TEST(PlanTidyTest, ANewTaskNeedsTwoTabs) {
  EXPECT_TRUE(PlanTidy(LooseTabs(),
                       {{Id("lone"), ToNewTask(u"Read about bees")},
                        {Id("feed"), ToNewTask(u"Read the news")}},
                       {})
                  .empty());
  // A page and a tab nested under it by hand make two.
  EXPECT_EQ(PlanTidy(LooseTabs(),
                     {{Id("grinders"), ToNewTask(u"Choose a grinder")},
                      {Id("review"), TidyTarget()}},
                     {Id("review")}),
            (std::vector<TidyMove>{
                {Id("grinders"), {}, ToNewTask(u"Choose a grinder")}}));
}

}  // namespace
}  // namespace uw
