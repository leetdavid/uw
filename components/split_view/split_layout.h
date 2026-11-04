// Copyright 2026 The uw Authors. All rights reserved.

#ifndef UW_COMPONENTS_SPLIT_VIEW_SPLIT_LAYOUT_H_
#define UW_COMPONENTS_SPLIT_VIEW_SPLIT_LAYOUT_H_

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "ui/gfx/geometry/rect.h"

namespace uw {

// Which way a cut divides its space: into a left and a right part, or into a
// top and a bottom part.
enum class SplitDirection { kSideBySide, kStacked };

// How a split view's panes are arranged, as in a terminal: every cut divides
// one part of the page area in two, and each part is a pane or holds more
// cuts. Panes are numbered in reading order, by their top-left corners from
// left to right and then top to bottom, and every change renumbers them.
class SplitLayout {
 public:
  // Where each pane went in a change: `to[old_number]` is its new number, or
  // nullopt for a pane that was taken out.
  struct Renumbering {
    std::vector<std::optional<size_t>> to;
  };

  // A gap between two parts, which a drag moves.
  struct Divider {
    size_t cut;
    SplitDirection direction;
    gfx::Rect bounds;
    // The space the cut divides.
    gfx::Rect span;
  };

  // Two panes, 0 then 1, the first taking `ratio` of the space.
  explicit SplitLayout(SplitDirection direction, double ratio = 0.5);
  SplitLayout(const SplitLayout& other);
  SplitLayout& operator=(const SplitLayout& other);
  SplitLayout(SplitLayout&&);
  SplitLayout& operator=(SplitLayout&&);
  ~SplitLayout();

  // The text form, such as "r(0.5,0,d(0.4,1,2))": a pane number, or a cut
  // with `r` for side by side or `d` for stacked, its ratio, and its two parts.
  static std::optional<SplitLayout> FromString(std::string_view text);
  std::string ToString() const;

  size_t pane_count() const;
  size_t cut_count() const;

  // The direction and ratio of the first cut, which is all a two-pane split
  // has.
  SplitDirection direction() const;
  double ratio() const;

  // Cuts `pane` in two the given way. The new pane takes the right or the
  // bottom half, and its number is returned. Existing panes keep their order.
  size_t Split(size_t pane, SplitDirection direction);

  // Takes `pane` out, and the part beside it takes its space. A layout keeps
  // at least two panes, so this needs three or more.
  Renumbering Remove(size_t pane);

  // The pane that takes most of `pane`'s space when it is removed: the
  // first pane of the part beside it.
  size_t Heir(size_t pane) const;

  // Sets how much of its space the first part of `cut` takes. Cuts are
  // numbered depth first, the outermost first. Panes keep their numbers, so
  // a divider can be dragged without its panes changing places; call
  // Renumber() once the drag ends.
  void SetRatio(size_t cut, double ratio);
  double ratio_of(size_t cut) const;

  // Turns `cut` the other way. Panes keep their numbers until Renumber().
  void SetDirection(size_t cut, SplitDirection direction);
  SplitDirection direction_of(size_t cut) const;

  // Numbers the panes in reading order again.
  Renumbering Renumber();

  // Each pane's bounds inside `bounds` with `gap` pixels between parts, in
  // pane order. Where there is room, no pane is narrower than
  // `min_size.width()` or shorter than `min_size.height()`.
  std::vector<gfx::Rect> PaneBounds(const gfx::Rect& bounds,
                                    int gap,
                                    const gfx::Size& min_size = {}) const;

  // Every gap between parts, outermost cut first.
  std::vector<Divider> Dividers(const gfx::Rect& bounds,
                                int gap,
                                const gfx::Size& min_size = {}) const;

  friend bool operator==(const SplitLayout& a, const SplitLayout& b);

  // A pane or a cut, defined in the .cc file.
  struct Node;

 private:
  SplitLayout();

  std::unique_ptr<Node> root_;
};

}  // namespace uw

#endif  // UW_COMPONENTS_SPLIT_VIEW_SPLIT_LAYOUT_H_
