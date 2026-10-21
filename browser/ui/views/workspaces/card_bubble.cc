// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/browser/ui/views/workspaces/card_bubble.h"

#include <utility>

#include "base/location.h"
#include "base/task/sequenced_task_runner.h"

namespace uw {

CardBubble::CardBubble(views::View* anchor,
                       views::BubbleBorder::Arrow arrow,
                       base::OnceClosure closing)
    : BubbleDialogDelegate(anchor, arrow), closing_(std::move(closing)) {}

CardBubble::~CardBubble() = default;

void CardBubble::Watch(views::Widget* widget) {
  observation_.Observe(widget);
}

void CardBubble::OnWidgetClosing(views::Widget* widget) {
  if (closing_) {
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, std::move(closing_));
  }
}

void CardBubble::OnWidgetDestroyed(views::Widget* widget) {
  observation_.Reset();
  base::SequencedTaskRunner::GetCurrentDefault()->DeleteSoon(FROM_HERE, this);
}

}  // namespace uw
