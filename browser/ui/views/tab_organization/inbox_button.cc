// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/browser/ui/views/tab_organization/inbox_button.h"

#include <algorithm>
#include <string>

#include "base/functional/bind.h"
#include "base/numerics/safe_conversions.h"
#include "base/strings/string_number_conversions.h"
#include "cc/paint/paint_flags.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/color/chrome_color_id.h"
#include "chrome/browser/ui/views/bubble/webui_bubble_manager.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/tab_strip_region_view.h"
#include "chrome/browser/ui/webui/top_chrome/webui_contents_wrapper.h"
#include "chrome/grit/generated_resources.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/color/color_id.h"
#include "ui/color/color_provider.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/color_utils.h"
#include "ui/gfx/font_list.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/gfx/paint_vector_icon.h"
#include "ui/gfx/scoped_canvas.h"
#include "ui/gfx/text_utils.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/bubble/bubble_border.h"
#include "ui/views/controls/highlight_path_generator.h"
#include "ui/views/view_class_properties.h"
#include "ui/views/view_utils.h"
#include "url/gurl.h"
#include "uw/browser/ui/glass/glass.h"
#include "uw/browser/ui/look/menu_card.h"
#include "uw/browser/ui/views/tab_organization/floating_sidebar.h"
#include "uw/browser/ui/vector_icons/vector_icons.h"
#include "uw/browser/ui/webui/uw_chat/uw_inbox_ui.h"
#include "uw/browser/workspaces/workspace_theme.h"

namespace uw {

DEFINE_ELEMENT_IDENTIFIER_VALUE(kInboxButtonElementId);

namespace {

constexpr int kSize = 28;
constexpr int kRadius = 8;
constexpr int kIconSize = 16;
constexpr int kBadgeHeight = 15;
constexpr int kBadgeRing = 2;
constexpr int kBadgeFontSize = 10;
// The icon is the tint's text color at 75%, and at 55% in a window that is not
// in front.
constexpr SkAlpha kIconAlpha = 0xBF;
constexpr SkAlpha kInactiveIconAlpha = 0x8C;
constexpr SkAlpha kHoverAlpha = 0x1A;
// The primary color at 12% fills the button while the Inbox is open.
constexpr SkAlpha kOpenAlpha = 0x1F;
// How far in from the page's top-left corner the Inbox shows while the
// sidebar is hidden.
constexpr int kCornerGap = 10;

InboxButton* FindIn(views::View* view) {
  if (auto* button = views::AsViewClass<InboxButton>(view)) {
    return button;
  }
  for (views::View* child : view->children()) {
    if (InboxButton* found = FindIn(child)) {
      return found;
    }
  }
  return nullptr;
}

}  // namespace

std::unique_ptr<InboxButton> InboxButton::Make(BrowserWindowInterface* window) {
  ChatAlerts* alerts = ChatAlerts::For(window->GetProfile());
  return alerts ? std::make_unique<InboxButton>(window, alerts) : nullptr;
}

// static
void InboxButton::OpenIn(BrowserWindowInterface* window) {
  if (InboxButton* button =
          FindIn(BrowserView::GetBrowserViewForBrowser(window))) {
    button->Show(/*by_page_corner=*/IsSidebarHidden(window));
  }
}

InboxButton::InboxButton(BrowserWindowInterface* window, ChatAlerts* alerts)
    : views::Button(base::BindRepeating(&InboxButton::Open,
                                        base::Unretained(this))),
      window_(window) {
  SetProperty(views::kElementIdentifierKey, kInboxButtonElementId);
  views::InstallRoundRectHighlightPathGenerator(this, gfx::Insets(), kRadius);
  alerts_.Observe(alerts);
  UpdateName();
}

InboxButton::~InboxButton() = default;

bool InboxButton::IsShowing() const {
  return bubble_ && bubble_->GetBubbleWidget() &&
         bubble_->GetBubbleWidget()->IsVisible();
}

content::WebContents* InboxButton::page() const {
  return bubble_ && bubble_->GetContentsWrapper()
             ? bubble_->GetContentsWrapper()->web_contents()
             : nullptr;
}

gfx::Size InboxButton::CalculatePreferredSize(
    const views::SizeBounds& available_size) const {
  return gfx::Size(kSize, kSize);
}

void InboxButton::PaintButtonContents(gfx::Canvas* canvas) {
  const ui::ColorProvider* colors = GetColorProvider();
  const SkColor text = colors->GetColor(kColorTabForegroundInactiveFrameActive);
  const SkColor accent = colors->GetColor(kColorMenuCardAccent);
  const bool open = bubble_widget_.IsObserving();
  cc::PaintFlags fill;
  fill.setAntiAlias(true);
  if (open || GetState() == STATE_HOVERED || GetState() == STATE_PRESSED) {
    fill.setColor(open ? SkColorSetA(accent, kOpenAlpha)
                       : SkColorSetA(text, kHoverAlpha));
    canvas->DrawRoundRect(GetLocalBounds(), kRadius, fill);
  }
  const bool active = GetWidget() && GetWidget()->ShouldPaintAsActive();
  {
    gfx::ScopedCanvas scoped(canvas);
    canvas->Translate(gfx::Vector2d((kSize - kIconSize) / 2,
                                    (kSize - kIconSize) / 2));
    gfx::PaintVectorIcon(
        canvas, kInboxIcon, kIconSize,
        open ? accent
             : SkColorSetA(text, active ? kIconAlpha : kInactiveIconAlpha));
  }
  const size_t unseen = alerts_.GetSource()->Unseen();
  if (unseen == 0) {
    return;
  }
  // At the icon's top right, ringed in the tint: the frame's color, as much of
  // it as covers the window's glass.
  const std::u16string count =
      unseen > 99 ? u"99+" : base::NumberToString16(unseen);
  const gfx::FontList font = gfx::FontList()
                                 .DeriveWithSizeDelta(
                                     kBadgeFontSize -
                                     gfx::FontList().GetFontSize())
                                 .DeriveWithWeight(gfx::Font::Weight::SEMIBOLD);
  const int width = std::max(
      kBadgeHeight, gfx::GetStringWidth(count, font) + kBadgeHeight / 2);
  const gfx::RectF badge(kSize - width, 0, width, kBadgeHeight);
  gfx::RectF ring = badge;
  ring.Outset(kBadgeRing);
  fill.setColor(SkColorSetA(
      colors->GetColor(active ? ui::kColorFrameActive
                              : ui::kColorFrameInactive),
      base::ClampRound<SkAlpha>(CoverOfWindow(window_) * 255)));
  canvas->DrawRoundRect(ring, ring.height() / 2, fill);
  fill.setColor(accent);
  canvas->DrawRoundRect(badge, kBadgeHeight / 2.0f, fill);
  canvas->DrawStringRectWithFlags(
      count, font,
      color_utils::IsDark(accent) ? SK_ColorWHITE
                                  : SkColorSetRGB(0x0A, 0x0A, 0x0A),
      gfx::ToEnclosingRect(badge), gfx::Canvas::TEXT_ALIGN_CENTER);
}

void InboxButton::AddedToWidget() {
  paint_as_active_ = GetWidget()->RegisterPaintAsActiveChangedCallback(
      base::BindRepeating(&InboxButton::SchedulePaint,
                          base::Unretained(this)));
}

void InboxButton::RemovedFromWidget() {
  paint_as_active_ = {};
}

void InboxButton::OnAlertsChanged() {
  UpdateName();
  SchedulePaint();
}

void InboxButton::OnWidgetDestroying(views::Widget* widget) {
  bubble_widget_.Reset();
  hover_lock_.reset();
  SchedulePaint();
}

void InboxButton::Open() {
  Show(/*by_page_corner=*/false);
}

void InboxButton::Show(bool by_page_corner) {
  if (bubble_widget_.IsObserving()) {
    return;
  }
  // A new page each time, as the theme picker's.
  bubble_ = WebUIBubbleManager::Create<UwInboxUI>(window_, GURL(kInboxUrl),
                                                  IDS_UW_INBOX);
  if (by_page_corner) {
    const gfx::Rect card = BrowserView::GetBrowserViewForBrowser(window_)
                               ->contents_container()
                               ->GetBoundsInScreen();
    bubble_->ShowBubble(
        gfx::Rect(card.x() + kCornerGap, card.y() + kCornerGap, 0, 0),
        views::BubbleBorder::TOP_LEFT);
  } else {
    bubble_->ShowBubble(this, views::BubbleBorder::TOP_CENTER);
  }
  views::Widget* widget = bubble_->GetBubbleWidget();
  if (!widget) {
    return;
  }
  bubble_widget_.Observe(widget);
  hover_lock_ =
      BrowserView::GetBrowserViewForBrowser(window_)
          ->tab_strip_view()
          ->GetExpandOnHoverLock(ExpandOnHoverLockType::kKeepCurrentState);
  ShowInThemeMode(widget, window_);
  // The page frosts itself while the window's glass is on.
  FrostPageBubble(widget, page());
  alerts_.GetSource()->SeeAll();
  SchedulePaint();
}

void InboxButton::UpdateName() {
  const size_t unseen = alerts_.GetSource()->Unseen();
  const std::u16string name =
      unseen ? l10n_util::GetPluralStringFUTF16(IDS_UW_INBOX_UNSEEN, unseen)
             : l10n_util::GetStringUTF16(IDS_UW_INBOX);
  SetTooltipText(name);
  GetViewAccessibility().SetName(name);
}

BEGIN_METADATA(InboxButton)
END_METADATA

}  // namespace uw
