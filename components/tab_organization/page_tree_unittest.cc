// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

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

TEST(PageTreeTest, OrdersScratchpadFirstTasksTogetherChildrenAfterParents) {
  PageTree tree({P("hotels", "trip"), P("show", "", "news"),
                 P("gliner", "models"), P("fare", "trip", "flights"),
                 P("news"), P("flights", "trip"), P("ollama", "models"),
                 P("weather")});
  EXPECT_EQ(Render(tree),
            (Lines{"news@-", "  show@-", "weather@-", "hotels@trip",
                   "flights@trip", "  fare@trip", "gliner@models",
                   "ollama@models"}));
}

TEST(PageTreeTest, DropsLinksAcrossTasksToMissingPagesAndInCycles) {
  PageTree tree({P("a", "", "b"), P("b", "", "a"), P("c", "trip", "a"),
                 P("d", "", "gone"), P("e", "", "e")});
  EXPECT_EQ(Render(tree), (Lines{"a@-", "  b@-", "d@-", "e@-", "c@trip"}));
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
  EXPECT_EQ(Render(tree)[3], "new@-");

  EXPECT_TRUE(tree.OpenBeside(Id("new"), Id("flights")));
  EXPECT_EQ(Render(tree),
            (Lines{"news@-", "  show@-", "weather@-", "flights@trip",
                   "  fare@trip", "new@trip", "hotels@trip", "gliner@models",
                   "ollama@models"}));
  EXPECT_FALSE(tree.OpenBeside(Id("new"), Id("new")));
  EXPECT_FALSE(tree.OpenBeside(Id("new"), Id("unknown")));
}

TEST(PageTreeTest, LinkOpensAsLastChildWithoutBreakingOtherLinks) {
  PageTree tree = Seed();
  std::vector<Page> strip = Strip(tree);
  strip.insert(strip.begin() + 1, P("link"));
  tree.Adopt(strip, {});
  EXPECT_EQ(Render(tree)[1], "  link@-");
  EXPECT_EQ(Render(tree)[2], "  show@-");

  EXPECT_TRUE(tree.SetParent(Id("link"), Id("news")));
  EXPECT_EQ(Render(tree)[1], "  show@-");
  EXPECT_EQ(Render(tree)[2], "  link@-");
  EXPECT_FALSE(tree.SetParent(Id("news"), Id("link")));
  EXPECT_FALSE(tree.SetParent(Id("link"), Id("unknown")));
}

TEST(PageTreeTest, LinkIntoAnotherTaskTakesItsParentsTask) {
  PageTree tree = Seed();
  EXPECT_TRUE(tree.SetParent(Id("weather"), Id("fare")));
  EXPECT_EQ(Render(tree),
            (Lines{"news@-", "  show@-", "flights@trip", "  fare@trip",
                   "    weather@trip", "hotels@trip", "gliner@models",
                   "ollama@models"}));
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

TEST(PageTreeTest, NewTaskFollowsItsSourceOrComesFirst) {
  PageTree tree = Seed();
  EXPECT_TRUE(tree.MoveToNewTask(Id("news"), "read"));
  EXPECT_EQ(Render(tree),
            (Lines{"weather@-", "news@read", "  show@read", "flights@trip",
                   "  fare@trip", "hotels@trip", "gliner@models",
                   "ollama@models"}));
  EXPECT_TRUE(tree.MoveToNewTask(Id("hotels"), "book"));
  EXPECT_EQ(tree.Tasks(),
            (std::vector<std::string>{"read", "trip", "book", "models"}));

  EXPECT_FALSE(tree.MoveToNewTask(Id("weather"), "trip"));
  EXPECT_FALSE(tree.MoveToNewTask(Id("weather"), ""));
  EXPECT_FALSE(tree.MoveToNewTask(Id("unknown"), "fresh"));
}

TEST(PageTreeTest, NewTaskFromAnEmptiedTaskKeepsItsPosition) {
  PageTree tree({P("x", "one"), P("y", "two"), P("z", "three")});
  EXPECT_TRUE(tree.MoveToNewTask(Id("y"), "fresh"));
  EXPECT_EQ(tree.Tasks(), (std::vector<std::string>{"one", "fresh", "three"}));
}

TEST(PageTreeTest, MoveToTaskAppendsTheBranchAndDetachesIt) {
  PageTree tree = Seed();
  EXPECT_TRUE(tree.MoveToTask(Id("show"), "trip"));
  EXPECT_EQ(Render(tree),
            (Lines{"news@-", "weather@-", "flights@trip", "  fare@trip",
                   "hotels@trip", "show@trip", "gliner@models",
                   "ollama@models"}));
  EXPECT_TRUE(tree.MoveToTask(Id("flights"), "models"));
  EXPECT_EQ(Render(tree),
            (Lines{"news@-", "weather@-", "hotels@trip", "show@trip",
                   "gliner@models", "ollama@models", "flights@models",
                   "  fare@models"}));
  EXPECT_TRUE(tree.MoveToTask(Id("fare"), ""));
  EXPECT_EQ(Render(tree)[2], "fare@-");
  EXPECT_FALSE(tree.MoveToTask(Id("fare"), "no-such-task"));
  EXPECT_FALSE(tree.MoveToTask(Id("unknown"), "trip"));
}

TEST(PageTreeTest, NestAndOutdentMoveABranchOneLevel) {
  PageTree tree = Seed();
  EXPECT_TRUE(tree.Nest(Id("hotels")));
  EXPECT_EQ(Render(tree)[3], "flights@trip");
  EXPECT_EQ(Render(tree)[4], "  fare@trip");
  EXPECT_EQ(Render(tree)[5], "  hotels@trip");
  EXPECT_TRUE(tree.Outdent(Id("fare")));
  EXPECT_EQ(Render(tree)[3], "flights@trip");
  EXPECT_EQ(Render(tree)[4], "  hotels@trip");
  EXPECT_EQ(Render(tree)[5], "fare@trip");
  EXPECT_FALSE(tree.Nest(Id("news")));
  EXPECT_FALSE(tree.Outdent(Id("news")));
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
  const PageTree before({P("p"), P("c1", "", "p"), P("g", "", "c1"),
                         P("c2", "", "p"), P("x")});
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

TEST(PageTreeTest, AnUngroupedTabDroppedBetweenTasksReturnsToTheScratchpad) {
  PageTree tree = Seed();
  std::vector<Page> strip = Strip(tree);
  MoveTo(strip, "weather", 5);
  tree.Adopt(strip, {Id("weather")});
  EXPECT_EQ(tree, Seed());
}

TEST(PageTreeTest, RegroupingATabDetachesItFromItsParent) {
  PageTree tree = Seed();
  std::vector<Page> strip = Strip(tree);
  Erase(strip, "fare");
  strip.push_back(P("fare", "models"));
  tree.Adopt(strip, {Id("fare")});
  EXPECT_EQ(Render(tree),
            (Lines{"news@-", "  show@-", "weather@-", "flights@trip",
                   "hotels@trip", "gliner@models", "ollama@models",
                   "fare@models"}));
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
  strip.insert(strip.begin() + 3, P("new", "check"));
  tree.Adopt(strip, {});
  ASSERT_EQ(Render(tree)[3], "new@check");

  tree.Restore(Seed());
  EXPECT_EQ(Render(tree),
            (Lines{"news@-", "  show@-", "weather@-", "new@-", "flights@trip",
                   "  fare@trip", "hotels@trip", "gliner@models",
                   "ollama@models"}));
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
