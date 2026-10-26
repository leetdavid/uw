// Copyright 2026 The uw Authors. All rights reserved.

#ifndef UW_BROWSER_UI_VIEWS_WORKSPACES_CARD_BUBBLE_H_
#define UW_BROWSER_UI_VIEWS_WORKSPACES_CARD_BUBBLE_H_

#include "base/functional/callback.h"
#include "base/scoped_observation.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/views/bubble/bubble_dialog_delegate_view.h"
#include "ui/views/widget/widget.h"
#include "ui/views/widget/widget_observer.h"

namespace uw {

// The bubble of a workspace card or menu. The switcher keeps its widget, and
// the bubble deletes itself once the widget is gone. `closing` runs on a
// later task when the widget starts to close.
class CardBubble : public views::BubbleDialogDelegate,
                   public views::WidgetObserver {
 public:
  CardBubble(views::View* anchor,
             views::BubbleBorder::Arrow arrow,
             base::OnceClosure closing);
  CardBubble(const CardBubble&) = delete;
  CardBubble& operator=(const CardBubble&) = delete;
  ~CardBubble() override;

  void Watch(views::Widget* widget);
  // Keeps the card `gap` above or below its anchor.
  void set_anchor_gap(int gap) { anchor_gap_ = gap; }

  // views::BubbleDialogDelegate:
  gfx::Rect GetAnchorRect() const override;

  // views::WidgetObserver:
  void OnWidgetClosing(views::Widget* widget) override;
  void OnWidgetDestroyed(views::Widget* widget) override;

 private:
  base::OnceClosure closing_;
  int anchor_gap_ = 0;
  base::ScopedObservation<views::Widget, views::WidgetObserver> observation_{
      this};
};

}  // namespace uw

#endif  // UW_BROWSER_UI_VIEWS_WORKSPACES_CARD_BUBBLE_H_
