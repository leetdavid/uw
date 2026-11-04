// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/components/split_view/split_layout.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include "base/check.h"
#include "base/check_op.h"
#include "base/strings/string_number_conversions.h"

namespace uw {

struct SplitLayout::Node {
  // A pane when it has no parts.
  size_t pane = 0;
  SplitDirection direction = SplitDirection::kSideBySide;
  double ratio = 0.5;
  std::unique_ptr<Node> first;
  std::unique_ptr<Node> second;

  bool is_pane() const { return !first; }

  std::unique_ptr<Node> Clone() const {
    auto copy = std::make_unique<Node>();
    copy->pane = pane;
    copy->direction = direction;
    copy->ratio = ratio;
    if (!is_pane()) {
      copy->first = first->Clone();
      copy->second = second->Clone();
    }
    return copy;
  }
};

namespace {

using Node = SplitLayout::Node;

constexpr size_t kMaxDepth = 64;

struct Box {
  double x, y, width, height;
};

size_t CountPanes(const Node& node) {
  return node.is_pane() ? 1
                        : CountPanes(*node.first) + CountPanes(*node.second);
}

size_t CountCuts(const Node& node) {
  return node.is_pane() ? 0
                        : 1 + CountCuts(*node.first) + CountCuts(*node.second);
}

Node* FindPane(Node& node, size_t pane, Node** parent) {
  if (node.is_pane()) {
    return node.pane == pane ? &node : nullptr;
  }
  for (Node* part : {node.first.get(), node.second.get()}) {
    if (Node* found = FindPane(*part, pane, parent)) {
      if (!*parent) {
        *parent = &node;
      }
      return found;
    }
  }
  return nullptr;
}

Node* FindCut(Node& node, size_t& countdown) {
  if (node.is_pane()) {
    return nullptr;
  }
  if (countdown == 0) {
    return &node;
  }
  --countdown;
  if (Node* found = FindCut(*node.first, countdown)) {
    return found;
  }
  return FindCut(*node.second, countdown);
}

void PlaceBoxes(Node& node, Box box, std::vector<std::pair<Box, Node*>>& out) {
  if (node.is_pane()) {
    out.emplace_back(box, &node);
    return;
  }
  Box first = box;
  Box second = box;
  if (node.direction == SplitDirection::kSideBySide) {
    first.width = box.width * node.ratio;
    second.x = box.x + first.width;
    second.width = box.width - first.width;
  } else {
    first.height = box.height * node.ratio;
    second.y = box.y + first.height;
    second.height = box.height - first.height;
  }
  PlaceBoxes(*node.first, first, out);
  PlaceBoxes(*node.second, second, out);
}

// The least room a part needs along `axis` for none of its panes to go
// under `min_size`.
int MinExtent(const Node& node,
              SplitDirection axis,
              int gap,
              const gfx::Size& min_size) {
  if (node.is_pane()) {
    return axis == SplitDirection::kSideBySide ? min_size.width()
                                               : min_size.height();
  }
  const int first = MinExtent(*node.first, axis, gap, min_size);
  const int second = MinExtent(*node.second, axis, gap, min_size);
  return node.direction == axis ? first + gap + second
                                : std::max(first, second);
}

// How much of `room` the first part of `node` takes.
int FirstPart(const Node& node, int room, int gap, const gfx::Size& min_size) {
  const int wanted =
      std::clamp(static_cast<int>(std::lround(room * node.ratio)), 0, room);
  const int least = MinExtent(*node.first, node.direction, gap, min_size);
  const int most =
      room - MinExtent(*node.second, node.direction, gap, min_size);
  return least <= most ? std::clamp(wanted, least, most) : wanted;
}

void Place(const Node& node,
           const gfx::Rect& bounds,
           int gap,
           const gfx::Size& min_size,
           std::vector<std::pair<size_t, gfx::Rect>>& panes,
           std::vector<SplitLayout::Divider>* dividers,
           size_t& cut) {
  if (node.is_pane()) {
    panes.emplace_back(node.pane, bounds);
    return;
  }
  const size_t this_cut = cut++;
  gfx::Rect first = bounds;
  gfx::Rect second = bounds;
  gfx::Rect divider = bounds;
  if (node.direction == SplitDirection::kSideBySide) {
    const int room = std::max(0, bounds.width() - gap);
    const int first_width = FirstPart(node, room, gap, min_size);
    first.set_width(first_width);
    divider.set_x(bounds.x() + first_width);
    divider.set_width(bounds.width() - room);
    second.set_x(divider.right());
    second.set_width(room - first_width);
  } else {
    const int room = std::max(0, bounds.height() - gap);
    const int first_height = FirstPart(node, room, gap, min_size);
    first.set_height(first_height);
    divider.set_y(bounds.y() + first_height);
    divider.set_height(bounds.height() - room);
    second.set_y(divider.bottom());
    second.set_height(room - first_height);
  }
  if (dividers) {
    dividers->push_back({this_cut, node.direction, divider, bounds});
  }
  Place(*node.first, first, gap, min_size, panes, dividers, cut);
  Place(*node.second, second, gap, min_size, panes, dividers, cut);
}

bool SameShape(const Node& a, const Node& b) {
  if (a.is_pane() || b.is_pane()) {
    return a.is_pane() && b.is_pane() && a.pane == b.pane;
  }
  return a.direction == b.direction && a.ratio == b.ratio &&
         SameShape(*a.first, *b.first) && SameShape(*a.second, *b.second);
}

void Write(const Node& node, std::string& out) {
  if (node.is_pane()) {
    out += base::NumberToString(node.pane);
    return;
  }
  out += node.direction == SplitDirection::kSideBySide ? "r(" : "d(";
  out += base::NumberToString(node.ratio);
  out += ",";
  Write(*node.first, out);
  out += ",";
  Write(*node.second, out);
  out += ")";
}

class Reader {
 public:
  explicit Reader(std::string_view text) : text_(text) {}

  std::unique_ptr<Node> Read(size_t depth) {
    if (depth > kMaxDepth || at_ >= text_.size()) {
      return nullptr;
    }
    auto node = std::make_unique<Node>();
    const char kind = text_[at_];
    if (kind == 'r' || kind == 'd') {
      ++at_;
      node->direction =
          kind == 'r' ? SplitDirection::kSideBySide : SplitDirection::kStacked;
      std::optional<double> ratio;
      if (!Take('(') || !(ratio = Number()) || !Take(',')) {
        return nullptr;
      }
      if (!(*ratio > 0 && *ratio < 1)) {
        return nullptr;
      }
      node->ratio = *ratio;
      node->first = Read(depth + 1);
      if (!node->first || !Take(',')) {
        return nullptr;
      }
      node->second = Read(depth + 1);
      if (!node->second || !Take(')')) {
        return nullptr;
      }
      return node;
    }
    const size_t start = at_;
    while (at_ < text_.size() && text_[at_] >= '0' && text_[at_] <= '9') {
      ++at_;
    }
    if (!base::StringToSizeT(text_.substr(start, at_ - start), &node->pane)) {
      return nullptr;
    }
    return node;
  }

  bool done() const { return at_ == text_.size(); }

 private:
  bool Take(char c) {
    if (at_ < text_.size() && text_[at_] == c) {
      ++at_;
      return true;
    }
    return false;
  }

  std::optional<double> Number() {
    const size_t start = at_;
    while (at_ < text_.size() &&
           ((text_[at_] >= '0' && text_[at_] <= '9') || text_[at_] == '.' ||
            text_[at_] == 'e' || text_[at_] == '-' || text_[at_] == '+')) {
      ++at_;
    }
    double value = 0;
    if (!base::StringToDouble(text_.substr(start, at_ - start), &value)) {
      return std::nullopt;
    }
    return value;
  }

  std::string_view text_;
  size_t at_ = 0;
};

void CollectPanes(const Node& node, std::vector<size_t>& out) {
  if (node.is_pane()) {
    out.push_back(node.pane);
    return;
  }
  CollectPanes(*node.first, out);
  CollectPanes(*node.second, out);
}

}  // namespace

SplitLayout::SplitLayout() = default;

SplitLayout::SplitLayout(SplitDirection direction, double ratio)
    : root_(std::make_unique<Node>()) {
  CHECK(ratio > 0 && ratio < 1);
  root_->direction = direction;
  root_->ratio = ratio;
  root_->first = std::make_unique<Node>();
  root_->first->pane = 0;
  root_->second = std::make_unique<Node>();
  root_->second->pane = 1;
}

SplitLayout::SplitLayout(const SplitLayout& other)
    : root_(other.root_->Clone()) {}

SplitLayout& SplitLayout::operator=(const SplitLayout& other) {
  if (this != &other) {
    root_ = other.root_->Clone();
  }
  return *this;
}

SplitLayout::SplitLayout(SplitLayout&&) = default;
SplitLayout& SplitLayout::operator=(SplitLayout&&) = default;
SplitLayout::~SplitLayout() = default;

// static
std::optional<SplitLayout> SplitLayout::FromString(std::string_view text) {
  Reader reader(text);
  std::unique_ptr<Node> root = reader.Read(0);
  if (!root || !reader.done() || root->is_pane()) {
    return std::nullopt;
  }
  std::vector<size_t> panes;
  CollectPanes(*root, panes);
  std::vector<size_t> sorted = panes;
  std::ranges::sort(sorted);
  for (size_t i = 0; i < sorted.size(); ++i) {
    if (sorted[i] != i) {
      return std::nullopt;
    }
  }
  SplitLayout layout;
  layout.root_ = std::move(root);
  const Renumbering renumbering = layout.Renumber();
  for (size_t i = 0; i < renumbering.to.size(); ++i) {
    if (renumbering.to[i] != i) {
      return std::nullopt;
    }
  }
  return layout;
}

std::string SplitLayout::ToString() const {
  std::string out;
  Write(*root_, out);
  return out;
}

size_t SplitLayout::pane_count() const {
  return CountPanes(*root_);
}

size_t SplitLayout::cut_count() const {
  return CountCuts(*root_);
}

SplitDirection SplitLayout::direction() const {
  return root_->direction;
}

double SplitLayout::ratio() const {
  return root_->ratio;
}

size_t SplitLayout::Split(size_t pane, SplitDirection direction) {
  const size_t count = pane_count();
  CHECK_LT(pane, count);
  Node* parent = nullptr;
  Node* target = FindPane(*root_, pane, &parent);
  CHECK(target);
  auto kept = std::make_unique<Node>();
  kept->pane = pane;
  auto added = std::make_unique<Node>();
  added->pane = count;
  target->direction = direction;
  target->ratio = 0.5;
  target->first = std::move(kept);
  target->second = std::move(added);
  const Renumbering renumbering = Renumber();
  return *renumbering.to[count];
}

SplitLayout::Renumbering SplitLayout::Remove(size_t pane) {
  const size_t count = pane_count();
  CHECK_GT(count, 2u);
  CHECK_LT(pane, count);
  Node* parent = nullptr;
  Node* target = FindPane(*root_, pane, &parent);
  CHECK(target);
  CHECK(parent);
  std::unique_ptr<Node> neighbor = parent->first.get() == target
                                       ? std::move(parent->second)
                                       : std::move(parent->first);
  *parent = std::move(*neighbor);
  Renumbering renumbering = Renumber();
  renumbering.to.resize(count);
  renumbering.to[pane] = std::nullopt;
  return renumbering;
}

size_t SplitLayout::Heir(size_t pane) const {
  Node* parent = nullptr;
  Node* target = FindPane(*root_, pane, &parent);
  CHECK(target);
  CHECK(parent);
  const Node& neighbor =
      parent->first.get() == target ? *parent->second : *parent->first;
  std::vector<size_t> panes;
  CollectPanes(neighbor, panes);
  return std::ranges::min(panes);
}

void SplitLayout::SetRatio(size_t cut, double ratio) {
  CHECK(ratio > 0 && ratio < 1);
  size_t countdown = cut;
  Node* node = FindCut(*root_, countdown);
  CHECK(node);
  node->ratio = ratio;
}

void SplitLayout::SetDirection(size_t cut, SplitDirection direction) {
  size_t countdown = cut;
  Node* node = FindCut(*root_, countdown);
  CHECK(node);
  node->direction = direction;
}

SplitDirection SplitLayout::direction_of(size_t cut) const {
  size_t countdown = cut;
  Node* node = FindCut(*root_, countdown);
  CHECK(node);
  return node->direction;
}

double SplitLayout::ratio_of(size_t cut) const {
  size_t countdown = cut;
  Node* node = FindCut(*root_, countdown);
  CHECK(node);
  return node->ratio;
}

std::vector<gfx::Rect> SplitLayout::PaneBounds(
    const gfx::Rect& bounds,
    int gap,
    const gfx::Size& min_size) const {
  std::vector<std::pair<size_t, gfx::Rect>> placed;
  size_t cut = 0;
  Place(*root_, bounds, gap, min_size, placed, nullptr, cut);
  std::vector<gfx::Rect> out(placed.size());
  for (const auto& [pane, rect] : placed) {
    out[pane] = rect;
  }
  return out;
}

std::vector<SplitLayout::Divider> SplitLayout::Dividers(
    const gfx::Rect& bounds,
    int gap,
    const gfx::Size& min_size) const {
  std::vector<std::pair<size_t, gfx::Rect>> placed;
  std::vector<Divider> dividers;
  size_t cut = 0;
  Place(*root_, bounds, gap, min_size, placed, &dividers, cut);
  return dividers;
}

SplitLayout::Renumbering SplitLayout::Renumber() {
  std::vector<std::pair<Box, Node*>> boxes;
  PlaceBoxes(*root_, {0, 0, 1, 1}, boxes);
  // Corners closer than this count as level, so rounding never reorders panes
  // that line up.
  constexpr double kLevel = 1e-6;
  std::ranges::stable_sort(boxes, [](const auto& a, const auto& b) {
    if (std::abs(a.first.y - b.first.y) > kLevel) {
      return a.first.y < b.first.y;
    }
    return a.first.x < b.first.x - kLevel;
  });
  size_t largest = 0;
  for (const auto& [box, node] : boxes) {
    largest = std::max(largest, node->pane);
  }
  Renumbering renumbering;
  renumbering.to.resize(largest + 1);
  for (size_t i = 0; i < boxes.size(); ++i) {
    renumbering.to[boxes[i].second->pane] = i;
    boxes[i].second->pane = i;
  }
  return renumbering;
}

bool operator==(const SplitLayout& a, const SplitLayout& b) {
  return SameShape(*a.root_, *b.root_);
}

}  // namespace uw
