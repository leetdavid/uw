// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#ifndef UW_BROWSER_UI_VIEWS_TAB_ORGANIZATION_FLOATING_SIDEBAR_H_
#define UW_BROWSER_UI_VIEWS_TAB_ORGANIZATION_FLOATING_SIDEBAR_H_

#include <memory>

#include "base/memory/raw_ptr.h"
#include "base/scoped_observation.h"
#include "ui/base/accelerators/accelerator.h"
#include "ui/events/event_handler.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/views/view_targeter_delegate.h"
#include "ui/views/widget/widget.h"
#include "ui/views/widget/widget_observer.h"

class BrowserView;
class BrowserWindowInterface;
class ExpandOnHoverLock;
class VerticalTabStripRegionView;

namespace ui {
class LayerSolidColor;
}

namespace views {
class FocusManager;
}

namespace uw {

class FloatingWindowButtons;

// What a hidden sidebar keeps of the window: the strip left of the page card.
// The pointer resting there brings the floating sidebar in.
inline constexpr int kHiddenSidebarEdge = 8;

// How far uw's window buttons reach from the floating card's leading edge,
// with the gap after them.
inline constexpr int kFloatingWindowButtonsEnd = 82;

// How far right of its floating card a hidden sidebar draws the card's shadow.
// It clips what lies left of the window, such as its rows while the card is
// out.
inline constexpr int kFloatingShadowRoom = 16;

// Whether the sidebar of `window` is hidden.
bool IsSidebarHidden(BrowserWindowInterface* window);

// Draws a hidden sidebar as Arc's floating sidebar: a card 8px in from the
// window's edges that slides in over the page, with window buttons uw draws.
// `region`, Chromium's sidebar, spans the edge and the card, and lays its rows
// out on the card wherever the card is, so they keep their width all the way
// in. The pointer finds the sidebar on the edge, and on the card while it is
// out. Esc sends the card away until the pointer leaves the sidebar.
class FloatingSidebar : public ui::AcceleratorTarget,
                        public ui::EventHandler,
                        public views::ViewTargeterDelegate,
                        public views::WidgetObserver {
 public:
  FloatingSidebar(VerticalTabStripRegionView* region,
                  BrowserView* browser_view);
  FloatingSidebar(const FloatingSidebar&) = delete;
  FloatingSidebar& operator=(const FloatingSidebar&) = delete;
  ~FloatingSidebar() override;

  // Where the sidebar lays out its rows: in `shown` while it is shown, on the
  // card in its place while it is hidden.
  gfx::Insets Margin(const gfx::Insets& shown) const;

  // Moves the rows with the card, and places the card, its shadow, and the
  // window buttons.
  void Layout();

  // Hides macOS's window buttons while the sidebar is hidden, and shows them
  // again with it.
  void UpdateWindowButtons();

  // Where the card is in the sidebar. While the sidebar is hidden and the card
  // is out, it lies left of the window.
  gfx::Rect CardBounds() const;

  FloatingWindowButtons* window_buttons() { return window_buttons_; }

  // ui::AcceleratorTarget:
  bool AcceleratorPressed(const ui::Accelerator& accelerator) override;
  bool CanHandleAccelerators() const override;

  // ui::EventHandler:
  void OnMouseEvent(ui::MouseEvent* event) override;

  // views::ViewTargeterDelegate:
  bool DoesIntersectRect(const views::View* target,
                         const gfx::Rect& rect) const override;

  // views::WidgetObserver:
  void OnWidgetDestroying(views::Widget* widget) override;

 private:
  class Card;

  bool Hidden() const;
  // How far the card has slid in, from 0 to 1.
  double Amount() const;
  void ListenForEsc(bool listen);
  // With Transparency, blurs the page under the card.
  void UpdateBlur(const gfx::Rect& card, bool floating);

  const raw_ptr<VerticalTabStripRegionView> region_;
  const raw_ptr<BrowserView> browser_view_;
  raw_ptr<Card> card_ = nullptr;
  raw_ptr<FloatingWindowButtons> window_buttons_ = nullptr;
  std::unique_ptr<ui::LayerSolidColor> blur_;
  raw_ptr<views::FocusManager> focus_manager_ = nullptr;
  std::unique_ptr<ExpandOnHoverLock> sent_away_;
  base::ScopedObservation<views::Widget, views::WidgetObserver>
      widget_observation_{this};
};

}  // namespace uw

#endif  // UW_BROWSER_UI_VIEWS_TAB_ORGANIZATION_FLOATING_SIDEBAR_H_
