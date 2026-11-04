// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/components/split_view/split_layout.h"

#include <optional>
#include <string>
#include <vector>

#include "testing/gtest/include/gtest/gtest.h"
#include "ui/gfx/geometry/rect.h"

namespace uw {
namespace {

SplitLayout Parse(const std::string& text) {
  std::optional<SplitLayout> layout = SplitLayout::FromString(text);
  CHECK(layout) << text;
  return *layout;
}

TEST(SplitLayoutTest, TwoPanesShareTheSpaceWithAGapBetween) {
  const SplitLayout layout(SplitDirection::kSideBySide);
  EXPECT_EQ(layout.pane_count(), 2u);
  EXPECT_EQ(layout.PaneBounds(gfx::Rect(0, 0, 1008, 700), 8),
            (std::vector<gfx::Rect>{gfx::Rect(0, 0, 500, 700),
                                    gfx::Rect(508, 0, 500, 700)}));
}

TEST(SplitLayoutTest, SplittingTheRightPaneDownAddsAPaneUnderIt) {
  SplitLayout layout(SplitDirection::kSideBySide);
  EXPECT_EQ(layout.Split(1, SplitDirection::kStacked), 2u);
  EXPECT_EQ(layout.ToString(), "r(0.5,0,d(0.5,1,2))");
  EXPECT_EQ(layout.PaneBounds(gfx::Rect(0, 0, 1008, 708), 8),
            (std::vector<gfx::Rect>{gfx::Rect(0, 0, 500, 708),
                                    gfx::Rect(508, 0, 500, 350),
                                    gfx::Rect(508, 358, 500, 350)}));
}

TEST(SplitLayoutTest, ANewPaneTakesItsPlaceInReadingOrder) {
  SplitLayout layout(SplitDirection::kSideBySide);
  // Under the left pane, so after the right one: its top is lower.
  EXPECT_EQ(layout.Split(0, SplitDirection::kStacked), 2u);
  EXPECT_EQ(layout.ToString(), "r(0.5,d(0.5,0,2),1)");
  // Right of the left pane's top half, so between the two left panes.
  EXPECT_EQ(layout.Split(0, SplitDirection::kSideBySide), 1u);
  EXPECT_EQ(layout.ToString(), "r(0.5,d(0.5,r(0.5,0,1),3),2)");
}

TEST(SplitLayoutTest, SplittingTwiceToTheRightMakesARow) {
  SplitLayout layout(SplitDirection::kSideBySide);
  EXPECT_EQ(layout.Split(1, SplitDirection::kSideBySide), 2u);
  EXPECT_EQ(layout.PaneBounds(gfx::Rect(0, 0, 1000, 100), 0),
            (std::vector<gfx::Rect>{gfx::Rect(0, 0, 500, 100),
                                    gfx::Rect(500, 0, 250, 100),
                                    gfx::Rect(750, 0, 250, 100)}));
}

TEST(SplitLayoutTest, ARemovedPaneGivesItsSpaceToTheOneBesideIt) {
  SplitLayout layout = Parse("r(0.5,0,d(0.5,1,2))");
  const SplitLayout::Renumbering renumbering = layout.Remove(1);
  EXPECT_EQ(layout.ToString(), "r(0.5,0,1)");
  EXPECT_EQ(renumbering.to,
            (std::vector<std::optional<size_t>>{0, std::nullopt, 1}));
}

TEST(SplitLayoutTest, TheFirstPaneBesideARemovedPaneTakesItsPlace) {
  const SplitLayout layout = Parse("r(0.5,0,d(0.5,1,2))");
  EXPECT_EQ(layout.Heir(0), 1u);
  EXPECT_EQ(layout.Heir(1), 2u);
  EXPECT_EQ(layout.Heir(2), 1u);
}

TEST(SplitLayoutTest, RemovingAPaneCanMoveAnotherAheadInReadingOrder) {
  // The left column is split, so its lower pane comes after the right pane.
  SplitLayout layout = Parse("r(0.5,d(0.5,0,2),1)");
  const SplitLayout::Renumbering renumbering = layout.Remove(0);
  // The lower pane grows to the top of the window and comes first.
  EXPECT_EQ(layout.ToString(), "r(0.5,0,1)");
  EXPECT_EQ(renumbering.to,
            (std::vector<std::optional<size_t>>{std::nullopt, 1, 0}));
}

TEST(SplitLayoutTest, MovingADividerCanReorderPanes) {
  // Left column cut at half, right column at 30%: the right column's lower
  // pane starts higher, so it comes before the left column's.
  SplitLayout layout = Parse("r(0.5,d(0.5,0,3),d(0.3,1,2))");
  layout.SetRatio(2, 0.7);
  // The panes keep their numbers while the divider moves.
  EXPECT_EQ(layout.ToString(), "r(0.5,d(0.5,0,3),d(0.7,1,2))");
  const SplitLayout::Renumbering renumbering = layout.Renumber();
  EXPECT_EQ(layout.ToString(), "r(0.5,d(0.5,0,2),d(0.7,1,3))");
  EXPECT_EQ(renumbering.to, (std::vector<std::optional<size_t>>{0, 1, 3, 2}));
  EXPECT_EQ(layout.ratio_of(2), 0.7);
}

TEST(SplitLayoutTest, NoPaneGoesUnderTheLeastSizeWhereThereIsRoom) {
  // The left pane wants 10% of the width; three panes of at least 200px
  // need it to give way.
  SplitLayout layout(SplitDirection::kSideBySide, 0.1);
  layout.Split(1, SplitDirection::kSideBySide);
  const std::vector<gfx::Rect> panes =
      layout.PaneBounds(gfx::Rect(0, 0, 1016, 100), 8, gfx::Size(200, 200));
  EXPECT_EQ(panes[0], gfx::Rect(0, 0, 200, 100));
  EXPECT_EQ(panes[1].width() + panes[2].width(), 1016 - 200 - 16);
  // Without the room for it, the ratio stands.
  EXPECT_EQ(
      layout.PaneBounds(gfx::Rect(0, 0, 500, 100), 8, gfx::Size(200, 200))[0]
          .width(),
      49);
}

TEST(SplitLayoutTest, EveryCutHasADivider) {
  const SplitLayout layout = Parse("r(0.5,0,d(0.5,1,2))");
  const std::vector<SplitLayout::Divider> dividers =
      layout.Dividers(gfx::Rect(0, 0, 1008, 708), 8);
  ASSERT_EQ(dividers.size(), 2u);
  EXPECT_EQ(dividers[0].cut, 0u);
  EXPECT_EQ(dividers[0].direction, SplitDirection::kSideBySide);
  EXPECT_EQ(dividers[0].bounds, gfx::Rect(500, 0, 8, 708));
  EXPECT_EQ(dividers[0].span, gfx::Rect(0, 0, 1008, 708));
  EXPECT_EQ(dividers[1].cut, 1u);
  EXPECT_EQ(dividers[1].direction, SplitDirection::kStacked);
  EXPECT_EQ(dividers[1].bounds, gfx::Rect(508, 350, 500, 8));
  EXPECT_EQ(dividers[1].span, gfx::Rect(508, 0, 500, 708));
}

TEST(SplitLayoutTest, TheTextFormComesBackAsTheSameLayout) {
  SplitLayout layout(SplitDirection::kStacked, 0.25);
  layout.Split(0, SplitDirection::kSideBySide);
  layout.SetRatio(1, 1.0 / 3);
  EXPECT_EQ(SplitLayout::FromString(layout.ToString()), layout);
}

TEST(SplitLayoutTest, BrokenTextIsRefused) {
  for (const char* text :
       {"", "0", "r(0.5,0)", "r(0.5,0,0)", "r(0.5,0,2)", "r(1,0,1)", "r(0,0,1)",
        "x(0.5,0,1)", "r(0.5,0,1))", "r(0.5,1,0)"}) {
    EXPECT_FALSE(SplitLayout::FromString(text)) << text;
  }
  std::string deep = "0";
  for (int i = 1; i <= 70; ++i) {
    deep = "d(0.5," + deep + "," + std::to_string(i) + ")";
  }
  EXPECT_FALSE(SplitLayout::FromString(deep));
}

}  // namespace
}  // namespace uw
