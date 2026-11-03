// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/browser/ui/views/tab_organization/inbox_button.h"

#include <algorithm>
#include <string>

#include "base/functional/bind.h"
#include "base/strings/string_number_conversions.h"
#include "cc/paint/paint_flags.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/views/bubble/webui_bubble_manager.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/tab_strip_region_view.h"
#include "chrome/browser/ui/webui/top_chrome/webui_contents_wrapper.h"
#include "chrome/grit/generated_resources.h"
#include "third_party/skia/include/core/SkClipOp.h"
#include "third_party/skia/include/core/SkRRect.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/color/color_provider.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/color_utils.h"
#include "ui/gfx/font_list.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/gfx/geometry/skia_conversions.h"
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
#include "uw/browser/ui/look/top_button.h"
#include "uw/browser/ui/vector_icons/vector_icons.h"
#include "uw/browser/ui/views/tab_organization/floating_sidebar.h"
#include "uw/browser/ui/webui/uw_chat/uw_inbox_ui.h"
#include "uw/browser/workspaces/workspace_theme.h"

namespace uw {

DEFINE_ELEMENT_IDENTIFIER_VALUE(kInboxButtonElementId);

namespace {

constexpr int kBadgeHeight = 14;
constexpr float kBadgeRing = 1.5f;
constexpr int kBadgeFontSize = 10;
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
  views::InstallRoundRectHighlightPathGenerator(this, gfx::Insets(),
                                                kTopButtonRadius);
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
  return gfx::Size(kTopButtonSize, kTopButtonSize);
}

void InboxButton::PaintButtonContents(gfx::Canvas* canvas) {
  const ui::ColorProvider* colors = GetColorProvider();
  const SkColor accent = colors->GetColor(kColorMenuCardAccent);
  const bool lit = bubble_widget_.IsObserving() || IsTopButtonLit(GetState());
  const size_t unread = alerts_.GetSource()->Unread();
  // At the icon's top right.
  const std::u16string count =
      unread > 99 ? u"99+" : base::NumberToString16(unread);
  const gfx::FontList font = gfx::FontList()
                                 .DeriveWithSizeDelta(
                                     kBadgeFontSize -
                                     gfx::FontList().GetFontSize())
                                 .DeriveWithWeight(gfx::Font::Weight::SEMIBOLD);
  const int width = std::max(
      kBadgeHeight, gfx::GetStringWidth(count, font) + kBadgeHeight / 2);
  const gfx::RectF badge(kTopButtonSize - width, 0, width, kBadgeHeight);
  cc::PaintFlags fill;
  fill.setAntiAlias(true);
  {
    gfx::ScopedCanvas scoped(canvas);
    if (unread) {
      // The fill and the icon leave a ring around the badge unpainted, so the
      // ring shows whatever is behind the button: the tint, or the glass.
      gfx::RectF ring = badge;
      ring.Outset(kBadgeRing);
      canvas->sk_canvas()->clipRRect(
          SkRRect::MakeRectXY(gfx::RectFToSkRect(ring), ring.height() / 2,
                              ring.height() / 2),
          SkClipOp::kDifference, /*do_anti_alias=*/true);
    }
    if (lit) {
      PaintTopButtonFill(canvas, *this);
    }
    const bool active = GetWidget() && GetWidget()->ShouldPaintAsActive();
    constexpr int kInset = (kTopButtonSize - kTopButtonIconSize) / 2;
    canvas->Translate(gfx::Vector2d(kInset, kInset));
    gfx::PaintVectorIcon(canvas, kInboxIcon, kTopButtonIconSize,
                         colors->GetColor(TopButtonIconColor(lit, active)));
  }
  if (unread == 0) {
    return;
  }
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
      base::BindRepeating(&InboxButton::SchedulePaint, base::Unretained(this)));
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
  SchedulePaint();
}

void InboxButton::UpdateName() {
  const size_t unread = alerts_.GetSource()->Unread();
  const std::u16string name =
      unread ? l10n_util::GetPluralStringFUTF16(IDS_UW_INBOX_UNSEEN, unread)
             : l10n_util::GetStringUTF16(IDS_UW_INBOX);
  SetTooltipText(name);
  GetViewAccessibility().SetName(name);
}

BEGIN_METADATA(InboxButton)
END_METADATA

}  // namespace uw
