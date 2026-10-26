// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/browser/ui/views/workspaces/card_bubble.h"

#include <utility>

#include "base/location.h"
#include "base/task/sequenced_task_runner.h"
#include "ui/gfx/geometry/outsets.h"
#include "ui/gfx/geometry/rect.h"

namespace uw {

CardBubble::CardBubble(views::View* anchor,
                       views::BubbleBorder::Arrow arrow,
                       base::OnceClosure closing)
    : BubbleDialogDelegate(anchor, arrow), closing_(std::move(closing)) {}

CardBubble::~CardBubble() = default;

void CardBubble::Watch(views::Widget* widget) {
  observation_.Observe(widget);
}

gfx::Rect CardBubble::GetAnchorRect() const {
  gfx::Rect anchor = BubbleDialogDelegate::GetAnchorRect();
  anchor.Outset(gfx::Outsets::VH(anchor_gap_, 0));
  return anchor;
}

void CardBubble::OnWidgetClosing(views::Widget* widget) {
  if (closing_) {
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, std::move(closing_));
  }
}

void CardBubble::OnWidgetDestroyed(views::Widget* widget) {
  observation_.Reset();
  // The anchor can outlive the card, and would tell its gone widget of theme
  // changes until this deletes.
  SetAnchorView(nullptr);
  base::SequencedTaskRunner::GetCurrentDefault()->DeleteSoon(FROM_HERE, this);
}

}  // namespace uw
