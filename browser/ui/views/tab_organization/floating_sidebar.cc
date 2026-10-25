// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/browser/ui/views/tab_organization/floating_sidebar.h"

#include <algorithm>
#include <memory>

#include "base/numerics/safe_conversions.h"
#include "cc/paint/paint_flags.h"
#include "chrome/browser/ui/animation/browser_animation_controller.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/color/chrome_color_id.h"
#include "chrome/browser/ui/tabs/vertical_tab_strip_state_controller.h"
#include "chrome/browser/ui/views/animations/tab_strip_animations.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/shadow_frame_view.h"
#include "chrome/browser/ui/views/frame/tab_strip_region_view.h"
#include "chrome/browser/ui/views/frame/vertical_tab_strip_region_view.h"
#include "third_party/skia/include/core/SkRRect.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/color/color_id.h"
#include "ui/color/color_provider.h"
#include "ui/compositor/layer.h"
#include "ui/events/event.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/gfx/geometry/rounded_corners_f.h"
#include "ui/gfx/geometry/skia_conversions.h"
#include "ui/gfx/geometry/vector2d.h"
#include "ui/gfx/scoped_canvas.h"
#include "ui/views/focus/focus_manager.h"
#include "ui/views/view.h"
#include "ui/views/view_class_properties.h"
#include "ui/views/view_shadow.h"
#include "ui/views/view_targeter.h"
#include "ui/views/view_utils.h"
#include "ui/views/widget/widget.h"
#include "uw/browser/ui/glass/glass.h"
#include "uw/browser/ui/look/page_card.h"
#include "uw/browser/ui/look/tint_painter.h"
#include "uw/browser/ui/look/window_buttons.h"
#include "uw/browser/ui/views/tab_organization/activity_card.h"
#include "uw/browser/ui/views/tab_organization/floating_window_buttons.h"
#include "uw/browser/ui/views/tab_organization/organize_bar.h"
#include "uw/browser/workspaces/workspace_theme.h"

namespace uw {
namespace {

// Where the window buttons sit on the card: where the shown sidebar has
// macOS's, centered on the toolbar beside them.
constexpr gfx::Vector2d kWindowButtonsOffset(12, 15);
// The ring around the card where the page card has no edge: the tint's text
// color at 6%.
constexpr SkAlpha kRingAlpha = 0x0F;
// How much the page blurs under the card with Transparency, as the desktop
// does under the shown sidebar.
constexpr float kBlurSigma = 17.0f;
// The activity card's corners, and how high it floats alone over the page.
constexpr int kActivityCardRadius = 10;
constexpr int kLoneCardElevation = 12;

const ui::Accelerator& EscKey() {
  static const ui::Accelerator esc(ui::VKEY_ESCAPE, ui::EF_NONE);
  return esc;
}

}  // namespace

// A card's surface: the window tint, as one sheet with the edge around the
// page, in rounded corners. A `see_through` card lets the page through as far
// as Transparency says.
class FloatingSidebar::Card : public views::View {
  METADATA_HEADER(Card, views::View)

 public:
  Card(BrowserView* browser_view, int radius, bool see_through)
      : browser_view_(browser_view),
        radius_(radius),
        see_through_(see_through) {
    SetProperty(views::kViewIgnoredByLayoutKey, true);
    SetVisible(false);
  }
  Card(const Card&) = delete;
  Card& operator=(const Card&) = delete;
  ~Card() override = default;

  // views::View:
  void OnPaint(gfx::Canvas* canvas) override {
    const ui::ColorProvider* colors = GetColorProvider();
    BrowserWindowInterface* window = browser_view_->browser();
    const gfx::RectF bounds(GetLocalBounds());
    {
      gfx::ScopedCanvas scoped_canvas(canvas);
      canvas->sk_canvas()->clipRRect(
          SkRRect::MakeRectXY(gfx::RectFToSkRect(bounds), radius_, radius_),
          /*do_anti_alias=*/true);
      PaintTint(canvas, this, browser_view_,
                colors->GetColor(GetWidget()->ShouldPaintAsActive()
                                     ? ui::kColorFrameActive
                                     : ui::kColorFrameInactive),
                ThemeOfWindow(window),
                see_through_ ? CoverOfWindow(window) : 1.0f);
    }
    SkColor ring = colors->GetColor(kColorPageCardEdge);
    if (SkColorGetA(ring) == SK_AlphaTRANSPARENT) {
      ring = SkColorSetA(
          colors->GetColor(kColorTabForegroundInactiveFrameActive), kRingAlpha);
    }
    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    flags.setStyle(cc::PaintFlags::kStroke_Style);
    flags.setStrokeWidth(1.0f);
    flags.setColor(ring);
    gfx::RectF edge = bounds;
    edge.Inset(0.5f);
    canvas->DrawRoundRect(edge, radius_ - 0.5f, flags);
  }

 private:
  const raw_ptr<BrowserView> browser_view_;
  const int radius_;
  const bool see_through_;
};

BEGIN_METADATA(FloatingSidebar, Card)
END_METADATA

bool IsSidebarHidden(BrowserWindowInterface* window) {
  const tabs::VerticalTabStripStateController* controller =
      tabs::VerticalTabStripStateController::From(window);
  return controller && controller->ShouldDisplayVerticalTabs() &&
         controller->IsCollapsed();
}

FloatingSidebar::FloatingSidebar(VerticalTabStripRegionView* region,
                                 BrowserView* browser_view)
    : region_(region), browser_view_(browser_view) {
  card_ = region_->AddChildViewAt(
      std::make_unique<Card>(browser_view, kPageCardRadius,
                             /*see_through=*/true),
      0);
  card_->SetCanProcessEventsWithinSubtree(false);
  window_buttons_ =
      region_->AddChildView(std::make_unique<FloatingWindowButtons>());
  window_buttons_->SetProperty(views::kViewIgnoredByLayoutKey, true);
  window_buttons_->SetVisible(false);
  for (views::View* child : region_->children()) {
    if (auto* bar = views::AsViewClass<OrganizeBar>(child)) {
      bar_ = bar;
    }
  }
  lone_ = region_->AddChildView(std::make_unique<Card>(
      browser_view, kActivityCardRadius, /*see_through=*/false));
  lone_->SetProperty(views::kViewIgnoredByLayoutKey, true);
  lone_->SetVisible(false);
  lone_shadow_ = std::make_unique<views::ViewShadow>(lone_, kLoneCardElevation);
  lone_shadow_->SetRoundedCornerRadius(kActivityCardRadius);
  region_->AddPreTargetHandler(this);
  region_->SetEventTargeter(std::make_unique<views::ViewTargeter>(this));
}

FloatingSidebar::~FloatingSidebar() {
  ListenForEsc(false);
  region_->RemovePreTargetHandler(this);
}

gfx::Insets FloatingSidebar::Margin(const gfx::Insets& shown) const {
  if (!Hidden()) {
    return shown;
  }
  return gfx::Insets::TLBR(kHiddenSidebarEdge + shown.top(),
                           kHiddenSidebarEdge + shown.left(),
                           kHiddenSidebarEdge + shown.bottom(),
                           region_->width() - kHiddenSidebarEdge -
                               region_->uncollapsed_width() + shown.right());
}

void FloatingSidebar::Layout() {
  const bool hidden = Hidden();
  region_->layer()->SetClipRect(
      hidden
          ? gfx::Rect(region_->width() + kFloatingShadowRoom, region_->height())
          : gfx::Rect());
  const bool floating = hidden && Amount() > 0.0;
  PlaceActivityCard(hidden && !floating);
  card_->SetVisible(floating);
  window_buttons_->SetVisible(floating);
  ListenForEsc(floating);
  const gfx::Rect card = CardBounds();
  UpdateBlur(card, floating);
  if (!hidden) {
    sent_away_.reset();
    // Chromium shadows its own sidebar.
    return;
  }
  // The rows lie on the card in its place. Move them with the card.
  const int shift = card.x() - kHiddenSidebarEdge;
  for (views::View* child : region_->children()) {
    if (!child->GetProperty(views::kViewIgnoredByLayoutKey)) {
      child->SetX(child->x() + shift);
    }
  }
  ShadowFrameView* shadow = region_->shadow_frame();
  shadow->SetShadowVisible(floating);
  if (!floating) {
    return;
  }
  card_->SetBoundsRect(card);
  shadow->SetBoundsRect(card);
  shadow->SetShadowCornerRadius(kPageCardRadius);
  shadow->SetShadowOpacity(1.0);
  window_buttons_->SetBoundsRect(
      gfx::Rect(card.origin() + kWindowButtonsOffset,
                window_buttons_->GetPreferredSize()));
}

void FloatingSidebar::UpdateWindowButtons(bool moving) {
  if (views::Widget* widget = region_->GetWidget()) {
    SetWindowButtonsHidden(widget, Hidden() || moving);
  }
}

bool FloatingSidebar::AcceleratorPressed(const ui::Accelerator& accelerator) {
  // Esc in the sidebar's own rows and fields is theirs.
  if (region_->Contains(focus_manager_->GetFocusedView())) {
    return false;
  }
  sent_away_ =
      region_->GetExpandOnHoverLock(ExpandOnHoverLockType::kForceCollapse);
  return true;
}

bool FloatingSidebar::CanHandleAccelerators() const {
  return focus_manager_ != nullptr;
}

void FloatingSidebar::OnMouseEvent(ui::MouseEvent* event) {
  if (sent_away_ && event->type() == ui::EventType::kMouseExited &&
      event->target() == region_.get()) {
    sent_away_.reset();
  }
}

bool FloatingSidebar::DoesIntersectRect(const views::View* target,
                                        const gfx::Rect& rect) const {
  if (lone_->GetVisible() && lone_->bounds().Intersects(rect)) {
    return true;
  }
  gfx::Rect area = target->GetLocalBounds();
  if (Hidden()) {
    area.set_width(std::max(kHiddenSidebarEdge, CardBounds().right()));
  }
  return area.Intersects(rect);
}

void FloatingSidebar::OnWidgetDestroying(views::Widget* widget) {
  ListenForEsc(false);
}

bool FloatingSidebar::Hidden() const {
  return IsSidebarHidden(browser_view_->browser());
}

double FloatingSidebar::Amount() const {
  const BrowserAnimationController* controller =
      BrowserAnimationController::From(browser_view_->browser());
  return controller
             ? controller
                   ->GetCurrentValue(TabStripAnimations::kVerticalTabStrip,
                                     TabStripAnimations::kTabStripHoverWidth)
                   .value_or(0.0)
             : 0.0;
}

gfx::Rect FloatingSidebar::CardBounds() const {
  const int width = region_->uncollapsed_width();
  const int x =
      kHiddenSidebarEdge -
      base::ClampRound((width + kHiddenSidebarEdge) * (1.0 - Amount()));
  return gfx::Rect(x, kHiddenSidebarEdge, width,
                   std::max(0, region_->height() - 2 * kHiddenSidebarEdge));
}

void FloatingSidebar::PlaceActivityCard(bool alone) {
  ActivityCard* card = bar_ ? bar_->card() : nullptr;
  if (!card) {
    return;
  }
  alone = alone && card->GetVisible();
  if (alone && card->parent() != lone_) {
    lone_->AddChildView(card->parent()->RemoveChildViewT(card));
  } else if (!alone && card->parent() == lone_) {
    bar_->AddChildViewAt(lone_->RemoveChildViewT(card), 0);
  }
  lone_->SetVisible(alone);
  if (!alone) {
    return;
  }
  // As wide as in the bar, 16px from the window's left and bottom edges.
  const int width = bar_->width();
  const int height = card->GetHeightForWidth(width);
  lone_->SetBoundsRect(gfx::Rect(
      2 * kHiddenSidebarEdge,
      region_->height() - 2 * kHiddenSidebarEdge - height, width, height));
  card->SetBoundsRect(lone_->GetLocalBounds());
}

void FloatingSidebar::ListenForEsc(bool listen) {
  if (listen == (focus_manager_ != nullptr)) {
    return;
  }
  if (!listen) {
    focus_manager_->UnregisterAccelerator(EscKey(), this);
    focus_manager_ = nullptr;
    widget_observation_.Reset();
    return;
  }
  views::Widget* widget = region_->GetWidget();
  if (!widget || !widget->GetFocusManager()) {
    return;
  }
  focus_manager_ = widget->GetFocusManager();
  focus_manager_->RegisterAccelerator(
      EscKey(), ui::AcceleratorManager::kNormalPriority, this);
  widget_observation_.Observe(widget);
}

void FloatingSidebar::UpdateBlur(const gfx::Rect& card, bool floating) {
  ui::Layer* parent = region_->layer()->parent();
  if (!floating || !parent || CoverOfWindow(browser_view_->browser()) >= 1.0f) {
    if (blur_) {
      blur_->SetVisible(false);
    }
    return;
  }
  if (!blur_) {
    blur_ = std::make_unique<ui::LayerSolidColor>();
    blur_->SetName("FloatingSidebarBlur");
    // Nearly clear: a layer that draws nothing blurs nothing.
    blur_->SetColor(SkColor4f::FromColor(SkColorSetA(SK_ColorBLACK, 1)));
    blur_->SetBackgroundBlur(kBlurSigma);
    blur_->SetRoundedCornerRadius(gfx::RoundedCornersF(kPageCardRadius));
  }
  if (blur_->parent() != parent) {
    if (blur_->parent()) {
      blur_->parent()->Remove(blur_.get());
    }
    parent->Add(blur_.get());
  }
  parent->StackBelow(blur_.get(), region_->layer());
  blur_->SetBounds(card + region_->layer()->bounds().OffsetFromOrigin());
  blur_->SetVisible(true);
}

}  // namespace uw
