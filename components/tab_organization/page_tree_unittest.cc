// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/components/tab_organization/page_tree.h"

#include <algorithm>
#include <string>
#include <vector>

#include "testing/gtest/include/gtest/gtest.h"

namespace uw {
namespace {

using Lines = std::vector<std::string>;

PageId Id(const char* id) {
  return PageId(id);
}

Page P(const char* id, const char* task = "", const char* parent = "") {
  return {PageId(id), task, PageId(parent)};
}

PageTree Seed() {
  return PageTree({P("news"), P("show", "", "news"), P("weather"),
                   P("flights", "trip"), P("fare", "trip", "flights"),
                   P("hotels", "trip"), P("gliner", "models"),
                   P("ollama", "models")});
}

// One line per page: two spaces per level, then id@task.
Lines Render(const PageTree& tree) {
  Lines lines;
  for (const Page& page : tree.pages()) {
    lines.push_back(std::string(tree.DepthOf(page.id) * 2, ' ') +
                    page.id.value() + "@" +
                    (page.task.empty() ? "-" : page.task));
  }
  return lines;
}

// The order and tasks Chromium would report, without parent links.
std::vector<Page> Strip(const PageTree& tree) {
  std::vector<Page> strip = tree.pages();
  for (Page& page : strip) {
    page.parent = PageId();
  }
  return strip;
}

void Erase(std::vector<Page>& strip, const char* id) {
  std::erase_if(strip, [&](const Page& page) { return page.id == Id(id); });
}

void MoveTo(std::vector<Page>& strip, const char* id, size_t index) {
  auto it = std::ranges::find(strip, Id(id), &Page::id);
  Page page = *it;
  strip.erase(it);
  strip.insert(strip.begin() + index, page);
}

TEST(PageTreeTest, OrdersTasksTogetherScratchpadLastChildrenAfterParents) {
  PageTree tree({P("hotels", "trip"), P("show", "", "news"),
                 P("gliner", "models"), P("fare", "trip", "flights"), P("news"),
                 P("flights", "trip"), P("ollama", "models"), P("weather")});
  EXPECT_EQ(Render(tree), (Lines{"hotels@trip", "flights@trip", "  fare@trip",
                                 "gliner@models", "ollama@models", "news@-",
                                 "  show@-", "weather@-"}));
}

TEST(PageTreeTest, DropsLinksAcrossTasksToMissingPagesAndInCycles) {
  PageTree tree({P("a", "", "b"), P("b", "", "a"), P("c", "trip", "a"),
                 P("d", "", "gone"), P("e", "", "e")});
  EXPECT_EQ(Render(tree), (Lines{"c@trip", "a@-", "  b@-", "d@-", "e@-"}));
}

TEST(PageTreeTest, ReportsBranchDepthTopTasksAndPreviousSibling) {
  const PageTree tree = Seed();
  EXPECT_EQ(tree.BranchOf(Id("flights")),
            (std::vector<PageId>{Id("flights"), Id("fare")}));
  EXPECT_TRUE(tree.BranchOf(Id("unknown")).empty());
  EXPECT_EQ(tree.DepthOf(Id("fare")), 1);
  EXPECT_EQ(tree.TopOf(Id("fare")), Id("flights"));
  EXPECT_EQ(tree.Tasks(), (std::vector<std::string>{"trip", "models"}));
  ASSERT_TRUE(tree.PreviousSibling(Id("hotels")));
  EXPECT_EQ(tree.PreviousSibling(Id("hotels"))->id, Id("flights"));
  EXPECT_FALSE(tree.PreviousSibling(Id("news")));
  EXPECT_FALSE(tree.PreviousSibling(Id("fare")));
}

TEST(PageTreeTest, BlankNewTabOpensBelowTheActiveBranch) {
  PageTree tree = Seed();
  std::vector<Page> strip = Strip(tree);
  strip.push_back(P("new"));
  tree.Adopt(strip, {});
  EXPECT_EQ(Render(tree).back(), "new@-");

  EXPECT_TRUE(tree.OpenBeside(Id("new"), Id("flights")));
  EXPECT_EQ(Render(tree),
            (Lines{"flights@trip", "  fare@trip", "new@trip", "hotels@trip",
                   "gliner@models", "ollama@models", "news@-", "  show@-",
                   "weather@-"}));
  EXPECT_FALSE(tree.OpenBeside(Id("new"), Id("new")));
  EXPECT_FALSE(tree.OpenBeside(Id("new"), Id("unknown")));
}

TEST(PageTreeTest, ALinkFromAnotherAppOpensAtTheTopOfTheScratchpad) {
  PageTree tree = Seed();
  std::vector<Page> strip = Strip(tree);
  strip.push_back(P("link"));
  tree.Adopt(strip, {});
  ASSERT_EQ(Render(tree).back(), "link@-");

  EXPECT_TRUE(tree.OpenAtTopOfScratchpad(Id("link")));
  EXPECT_EQ(Render(tree), (Lines{"flights@trip", "  fare@trip", "hotels@trip",
                                 "gliner@models", "ollama@models", "link@-",
                                 "news@-", "  show@-", "weather@-"}));
  EXPECT_FALSE(tree.OpenAtTopOfScratchpad(Id("unknown")));
}

TEST(PageTreeTest, LinkOpensAsLastChildWithoutBreakingOtherLinks) {
  PageTree tree = Seed();
  std::vector<Page> strip = Strip(tree);
  strip.insert(strip.begin() + 6, P("link"));
  tree.Adopt(strip, {});
  EXPECT_EQ(Render(tree)[6], "  link@-");
  EXPECT_EQ(Render(tree)[7], "  show@-");

  EXPECT_TRUE(tree.SetParent(Id("link"), Id("news")));
  EXPECT_EQ(Render(tree)[6], "  show@-");
  EXPECT_EQ(Render(tree)[7], "  link@-");
  EXPECT_FALSE(tree.SetParent(Id("news"), Id("link")));
  EXPECT_FALSE(tree.SetParent(Id("link"), Id("unknown")));
}

TEST(PageTreeTest, NewChildTabOpensFirstUnderItsParent) {
  PageTree tree = Seed();
  std::vector<Page> strip = Strip(tree);
  strip.push_back(P("new"));
  tree.Adopt(strip, {});

  EXPECT_TRUE(tree.OpenUnder(Id("new"), Id("flights")));
  EXPECT_EQ(Render(tree),
            (Lines{"flights@trip", "  new@trip", "  fare@trip", "hotels@trip",
                   "gliner@models", "ollama@models", "news@-", "  show@-",
                   "weather@-"}));
  EXPECT_TRUE(tree.OpenUnder(Id("new"), Id("weather")));
  EXPECT_EQ(Render(tree).back(), "  new@-");
  EXPECT_FALSE(tree.OpenUnder(Id("weather"), Id("new")));
  EXPECT_FALSE(tree.OpenUnder(Id("new"), Id("unknown")));
}

TEST(PageTreeTest, LinkIntoAnotherTaskTakesItsParentsTask) {
  PageTree tree = Seed();
  EXPECT_TRUE(tree.SetParent(Id("weather"), Id("fare")));
  EXPECT_EQ(
      Render(tree),
      (Lines{"flights@trip", "  fare@trip", "    weather@trip", "hotels@trip",
             "gliner@models", "ollama@models", "news@-", "  show@-"}));
}

TEST(PageTreeTest, ClosingAParentMovesItsChildrenUpOneLevel) {
  PageTree tree({P("a"), P("b", "", "a"), P("c", "", "b"), P("d", "", "b"),
                 P("e", "", "a")});
  std::vector<Page> strip = Strip(tree);
  Erase(strip, "b");
  tree.Adopt(strip, {});
  EXPECT_EQ(Render(tree), (Lines{"a@-", "  c@-", "  d@-", "  e@-"}));

  Erase(strip, "a");
  tree.Adopt(strip, {});
  EXPECT_EQ(Render(tree), (Lines{"c@-", "d@-", "e@-"}));
}

TEST(PageTreeTest, ANewTaskComesAfterEveryOtherTask) {
  PageTree tree = Seed();
  EXPECT_TRUE(tree.MoveToNewTask(Id("news"), "read"));
  EXPECT_EQ(Render(tree), (Lines{"flights@trip", "  fare@trip", "hotels@trip",
                                 "gliner@models", "ollama@models", "news@read",
                                 "  show@read", "weather@-"}));
  EXPECT_TRUE(tree.MoveToNewTask(Id("hotels"), "book"));
  EXPECT_EQ(tree.Tasks(),
            (std::vector<std::string>{"trip", "models", "read", "book"}));

  EXPECT_FALSE(tree.MoveToNewTask(Id("weather"), "trip"));
  EXPECT_FALSE(tree.MoveToNewTask(Id("weather"), ""));
  EXPECT_FALSE(tree.MoveToNewTask(Id("unknown"), "fresh"));
}

TEST(PageTreeTest, ANewTaskFromAnEmptiedTaskComesLastToo) {
  PageTree tree({P("x", "one"), P("y", "two"), P("z", "three")});
  EXPECT_TRUE(tree.MoveToNewTask(Id("y"), "fresh"));
  EXPECT_EQ(tree.Tasks(), (std::vector<std::string>{"one", "three", "fresh"}));
}

TEST(PageTreeTest, MoveToTaskAppendsTheBranchAndDetachesIt) {
  PageTree tree = Seed();
  EXPECT_TRUE(tree.MoveToTask(Id("show"), "trip"));
  EXPECT_EQ(Render(tree),
            (Lines{"flights@trip", "  fare@trip", "hotels@trip", "show@trip",
                   "gliner@models", "ollama@models", "news@-", "weather@-"}));
  EXPECT_TRUE(tree.MoveToTask(Id("flights"), "models"));
  EXPECT_EQ(Render(tree),
            (Lines{"hotels@trip", "show@trip", "gliner@models", "ollama@models",
                   "flights@models", "  fare@models", "news@-", "weather@-"}));
  // The Scratchpad takes a branch at its end, the foot of the strip.
  EXPECT_TRUE(tree.MoveToTask(Id("fare"), ""));
  EXPECT_EQ(Render(tree).back(), "fare@-");
  EXPECT_FALSE(tree.MoveToTask(Id("fare"), "no-such-task"));
  EXPECT_FALSE(tree.MoveToTask(Id("unknown"), "trip"));
}

TEST(PageTreeTest, NestAndOutdentMoveABranchOneLevel) {
  PageTree tree = Seed();
  EXPECT_TRUE(tree.Nest(Id("hotels")));
  EXPECT_EQ(Render(tree)[0], "flights@trip");
  EXPECT_EQ(Render(tree)[1], "  fare@trip");
  EXPECT_EQ(Render(tree)[2], "  hotels@trip");
  EXPECT_TRUE(tree.Outdent(Id("fare")));
  EXPECT_EQ(Render(tree)[0], "flights@trip");
  EXPECT_EQ(Render(tree)[1], "  hotels@trip");
  EXPECT_EQ(Render(tree)[2], "fare@trip");
  EXPECT_FALSE(tree.Nest(Id("news")));
  EXPECT_FALSE(tree.Outdent(Id("news")));
}

TEST(PageTreeTest, MoveUpAndDownSwapABranchWithItsSibling) {
  PageTree tree = Seed();
  EXPECT_TRUE(tree.MoveUp(Id("hotels")));
  EXPECT_EQ(Render(tree)[0], "hotels@trip");
  EXPECT_EQ(Render(tree)[1], "flights@trip");
  EXPECT_EQ(Render(tree)[2], "  fare@trip");
  EXPECT_TRUE(tree.MoveDown(Id("hotels")));
  EXPECT_EQ(Render(tree)[0], "flights@trip");
  EXPECT_EQ(Render(tree)[1], "  fare@trip");
  EXPECT_EQ(Render(tree)[2], "hotels@trip");
  EXPECT_FALSE(tree.MoveUp(Id("flights")));
  EXPECT_FALSE(tree.MoveDown(Id("hotels")));
  EXPECT_FALSE(tree.MoveUp(Id("fare")));
  EXPECT_FALSE(tree.MoveDown(Id("fare")));
  ASSERT_TRUE(tree.NextSibling(Id("news")));
  EXPECT_EQ(tree.NextSibling(Id("news"))->id, Id("weather"));
}

TEST(PageTreeTest, DraggingAChildPastAnotherTabDetachesIt) {
  PageTree tree({P("a"), P("b", "", "a"), P("c", "", "a"), P("d")});
  std::vector<Page> strip = Strip(tree);
  MoveTo(strip, "b", 3);
  tree.Adopt(strip, {Id("b")});
  EXPECT_EQ(Render(tree), (Lines{"a@-", "  c@-", "d@-", "b@-"}));
}

TEST(PageTreeTest, ReorderingSiblingsKeepsThemNested) {
  PageTree tree({P("a"), P("b", "", "a"), P("c", "", "a"), P("d")});
  std::vector<Page> strip = Strip(tree);
  MoveTo(strip, "b", 2);
  tree.Adopt(strip, {Id("b")});
  EXPECT_EQ(Render(tree), (Lines{"a@-", "  c@-", "  b@-", "d@-"}));
}

TEST(PageTreeTest, DroppingATabIntoABranchJoinsItAndKeepsOtherLinks) {
  const PageTree before(
      {P("p"), P("c1", "", "p"), P("g", "", "c1"), P("c2", "", "p"), P("x")});
  PageTree tree = before;
  std::vector<Page> strip = Strip(tree);
  MoveTo(strip, "x", 2);
  tree.Adopt(strip, {Id("x")});
  EXPECT_EQ(Render(tree),
            (Lines{"p@-", "  c1@-", "    x@-", "    g@-", "  c2@-"}));

  tree = before;
  strip = Strip(tree);
  MoveTo(strip, "x", 3);
  tree.Adopt(strip, {Id("x")});
  EXPECT_EQ(Render(tree),
            (Lines{"p@-", "  c1@-", "    g@-", "  x@-", "  c2@-"}));
}

TEST(PageTreeTest, DraggingAParentAwayPromotesItsChildren) {
  PageTree tree({P("p"), P("c", "", "p"), P("q")});
  std::vector<Page> strip = Strip(tree);
  MoveTo(strip, "p", 2);
  tree.Adopt(strip, {Id("p")});
  EXPECT_EQ(Render(tree), (Lines{"c@-", "q@-", "p@-"}));
}

TEST(PageTreeTest, AMovedBranchKeepsItsShape) {
  PageTree tree({P("p"), P("c", "", "p"), P("q"), P("r")});
  std::vector<Page> strip = Strip(tree);
  MoveTo(strip, "p", 3);
  MoveTo(strip, "c", 3);
  tree.Adopt(strip, {Id("p"), Id("c")});
  EXPECT_EQ(Render(tree), (Lines{"q@-", "r@-", "p@-", "  c@-"}));
}

TEST(PageTreeTest, MovingLeftAfterTheLastChildLeavesTheBranch) {
  PageTree tree({P("a"), P("b", "", "a"), P("c", "", "a"), P("d")});
  std::vector<Page> strip = Strip(tree);
  MoveTo(strip, "b", 2);
  tree.Adopt(strip, {Id("b")}, {.levels = -1});
  EXPECT_EQ(Render(tree), (Lines{"a@-", "  c@-", "b@-", "d@-"}));
}

TEST(PageTreeTest, AStraightDragNeverNestsUnderATabItPasses) {
  const PageTree before({P("a"), P("b", "", "a"), P("c"), P("d")});
  PageTree tree = before;
  std::vector<Page> strip = Strip(tree);
  MoveTo(strip, "b", 2);
  tree.Adopt(strip, {Id("b")});
  EXPECT_EQ(Render(tree), (Lines{"a@-", "c@-", "b@-", "d@-"}));

  tree = before;
  tree.Adopt(strip, {Id("b")}, {.levels = 1});
  EXPECT_EQ(Render(tree), (Lines{"a@-", "c@-", "  b@-", "d@-"}));
}

TEST(PageTreeTest, SidewaysStaysBetweenTheTabBelowAndOneUnderTheTabAbove) {
  const PageTree before(
      {P("p"), P("a", "", "p"), P("b", "", "p"), P("x"), P("y")});
  std::vector<Page> strip = Strip(before);
  MoveTo(strip, "x", 2);

  PageTree tree = before;
  tree.Adopt(strip, {Id("x")}, {.levels = -3});
  EXPECT_EQ(Render(tree), (Lines{"p@-", "  a@-", "  x@-", "  b@-", "y@-"}));

  tree = before;
  tree.Adopt(strip, {Id("x")}, {.levels = 5});
  EXPECT_EQ(Render(tree), (Lines{"p@-", "  a@-", "    x@-", "  b@-", "y@-"}));

  strip = Strip(before);
  MoveTo(strip, "x", 3);
  tree = before;
  tree.Adopt(strip, {Id("x")}, {.levels = 1});
  EXPECT_EQ(Render(tree), (Lines{"p@-", "  a@-", "  b@-", "  x@-", "y@-"}));
}

TEST(PageTreeTest, NothingLandsInsideACollapsedBranch) {
  const PageTree before(
      {P("p"), P("a", "", "p"), P("b", "", "p"), P("q"), P("x")});
  const Lines after = {"p@-", "  a@-", "  b@-", "x@-", "q@-"};
  for (size_t index : {size_t{1}, size_t{2}, size_t{3}}) {
    std::vector<Page> strip = Strip(before);
    MoveTo(strip, "x", index);
    PageTree tree = before;
    tree.Adopt(strip, {Id("x")}, {.levels = 1, .collapsed = {Id("p")}});
    EXPECT_EQ(Render(tree), after) << index;
  }
}

TEST(PageTreeTest, ADraggedBranchKeepsItsShapeSideways) {
  PageTree tree(
      {P("p"), P("c", "", "p"), P("g", "", "c"), P("d", "", "p"), P("q")});
  std::vector<Page> strip = Strip(tree);
  MoveTo(strip, "q", 0);
  tree.Adopt(strip, {Id("p"), Id("c"), Id("g"), Id("d")}, {.levels = 1});
  EXPECT_EQ(Render(tree),
            (Lines{"q@-", "  p@-", "    c@-", "      g@-", "    d@-"}));
}

TEST(PageTreeTest, ADraggedBranchGoesNoDeeperThanTheSidebarShows) {
  PageTree tree({P("l0"), P("l1", "", "l0"), P("l2", "", "l1"),
                 P("l3", "", "l2"), P("l4", "", "l3"), P("b"), P("b1", "", "b"),
                 P("b2", "", "b1")});
  tree.Adopt(Strip(tree), {Id("b"), Id("b1"), Id("b2")}, {.levels = 9});
  EXPECT_EQ(tree.DepthOf(Id("b")), kMaxShownDepth - 2);
  EXPECT_EQ(tree.DepthOf(Id("b2")), kMaxShownDepth);
}

TEST(PageTreeTest, DroppedTabsSideBySideLandAsSiblings) {
  PageTree tree({P("p"), P("a", "", "p"), P("x"), P("x1", "", "x"), P("y")});
  tree.Adopt(Strip(tree), {Id("x"), Id("x1"), Id("y")}, {.levels = 1});
  EXPECT_EQ(Render(tree),
            (Lines{"p@-", "  a@-", "  x@-", "    x1@-", "  y@-"}));
}

TEST(PageTreeTest, ADraggedTabKeepsTheParentItHadWhenTheDragBegan) {
  const PageTree before({P("p"), P("a", "", "p"), P("b", "", "p"), P("q")});
  PageTree tree = before;
  std::vector<Page> strip = Strip(tree);
  MoveTo(strip, "b", 3);
  tree.Adopt(strip, {Id("b")}, {.before = &before});
  EXPECT_EQ(Render(tree), (Lines{"p@-", "  a@-", "q@-", "b@-"}));

  strip = Strip(tree);
  MoveTo(strip, "b", 2);
  tree.Adopt(strip, {Id("b")}, {.before = &before});
  EXPECT_EQ(Render(tree), (Lines{"p@-", "  a@-", "  b@-", "q@-"}));
}

TEST(PageTreeTest, AnUngroupedTabDroppedBetweenTasksReturnsToTheScratchpad) {
  PageTree tree = Seed();
  std::vector<Page> strip = Strip(tree);
  MoveTo(strip, "weather", 3);
  tree.Adopt(strip, {Id("weather")});
  // It lands at the top of the Scratchpad, the nearest place it can be.
  EXPECT_EQ(Render(tree), (Lines{"flights@trip", "  fare@trip", "hotels@trip",
                                 "gliner@models", "ollama@models", "weather@-",
                                 "news@-", "  show@-"}));
}

TEST(PageTreeTest, RegroupingATabDetachesItFromItsParent) {
  PageTree tree = Seed();
  std::vector<Page> strip = Strip(tree);
  Erase(strip, "fare");
  strip.push_back(P("fare", "models"));
  tree.Adopt(strip, {Id("fare")});
  EXPECT_EQ(Render(tree), (Lines{"flights@trip", "hotels@trip", "gliner@models",
                                 "ollama@models", "fare@models", "news@-",
                                 "  show@-", "weather@-"}));
}

TEST(PageTreeTest, RestoreReturnsABranchToItsTask) {
  PageTree tree = Seed();
  ASSERT_TRUE(tree.MoveToTask(Id("flights"), "models"));
  tree.Restore(Seed());
  EXPECT_EQ(tree, Seed());
}

TEST(PageTreeTest, RestoreKeepsPagesOpenedSinceAndDropsAVanishedTask) {
  PageTree tree = Seed();
  ASSERT_TRUE(tree.MoveToNewTask(Id("weather"), "check"));
  std::vector<Page> strip = Strip(tree);
  strip.insert(strip.begin() + 6, P("new", "check"));
  tree.Adopt(strip, {});
  ASSERT_EQ(Render(tree)[6], "new@check");

  tree.Restore(Seed());
  EXPECT_EQ(Render(tree), (Lines{"flights@trip", "  fare@trip", "hotels@trip",
                                 "gliner@models", "ollama@models", "news@-",
                                 "  show@-", "weather@-", "new@-"}));
}

TEST(PageTreeTest, RestoreSkipsClosedPagesAndUsesASurvivingAncestor) {
  const PageTree before({P("a"), P("b", "", "a"), P("c", "", "b")});
  PageTree tree = before;
  ASSERT_TRUE(tree.MoveToNewTask(Id("c"), "task"));
  std::vector<Page> strip = Strip(tree);
  Erase(strip, "b");
  tree.Adopt(strip, {});

  tree.Restore(before);
  EXPECT_EQ(Render(tree), (Lines{"a@-", "  c@-"}));
}

}  // namespace
}  // namespace uw
