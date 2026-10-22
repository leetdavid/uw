// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/browser/ui/views/workspaces/workspace_switcher.h"

#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "base/strings/utf_string_conversions.h"
#include "cc/paint/paint_flags.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/color/chrome_color_id.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/tab_strip_region_view.h"
#include "chrome/browser/ui/views/frame/vertical_tab_strip_region_view.h"
#include "chrome/grit/generated_resources.h"
#include "ui/base/accelerators/accelerator.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/color/color_provider.h"
#include "ui/compositor/layer.h"
#include "ui/gfx/animation/tween.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/font_list.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/controls/label.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/layout/fill_layout.h"
#include "ui/views/view_utils.h"
#include "ui/views/widget/widget.h"
#include "uw/browser/tab_organization/commands.h"
#include "uw/browser/ui/look/menu_card.h"
#include "uw/browser/ui/views/tab_organization/window_commands.h"
#include "uw/browser/ui/views/workspaces/card_bubble.h"
#include "uw/browser/ui/views/workspaces/workspace_cards.h"
#include "uw/browser/ui/views/workspaces/workspace_swipe.h"
#include "uw/browser/workspaces/workspace_list.h"
#include "uw/browser/workspaces/workspace_window.h"

namespace uw {
namespace {

constexpr int kIconSize = 28;
constexpr int kDotSlot = 12;
constexpr int kDotSize = 6;
constexpr int kSpacing = 4;
constexpr int kRadius = 9;
constexpr int kEmojiSize = 15;
constexpr int kRingWidth = 2;
// Four icons fit between Organize and the new button.
constexpr size_t kMostIcons = 4;
constexpr float kDimOpacity = 0.6f;
constexpr float kDimGrayscale = 0.35f;
// The tint's text color at about 55%, as the new button's caret is.
constexpr SkAlpha kDotAlpha = 0x8C;
// Under the pointer an icon lights on a fill of white at 30%, against the
// current one's 46%.
constexpr float kHoverFill = 30.0f / 46.0f;

// A workspace's icon in the switcher, or a dot from five workspaces on.
class WorkspaceIcon : public views::Button {
  METADATA_HEADER(WorkspaceIcon, views::Button)

 public:
  WorkspaceIcon(PressedCallback callback,
                BrowserWindowInterface* window,
                const WorkspaceList::Workspace& workspace,
                int index,
                bool current,
                bool dot)
      : Button(std::move(callback)),
        window_(window),
        id_(workspace.id),
        name_(base::UTF8ToUTF16(workspace.name)),
        index_(index),
        current_(current),
        dot_(dot) {
    SetPreferredSize(gfx::Size(dot ? kDotSlot : kIconSize, kIconSize));
    SetTooltipText(name_);
    GetViewAccessibility().SetName(base::UTF8ToUTF16(workspace.name));
    GetViewAccessibility().SetIsSelected(current);
    SetPaintToLayer();
    layer()->SetFillsBoundsOpaquely(false);
    if (!dot) {
      SetLayoutManager(std::make_unique<views::FillLayout>());
      auto* emoji = AddChildView(
          std::make_unique<views::Label>(base::UTF8ToUTF16(workspace.icon)));
      const gfx::FontList base;
      emoji->SetFontList(
          base.DeriveWithSizeDelta(kEmojiSize - base.GetFontSize()));
      emoji->SetBackgroundColor(SK_ColorTRANSPARENT);
      emoji->SetSubpixelRenderingEnabled(false);
    }
    Update();
  }

  const std::string& id() const { return id_; }

  void SetRinged(bool ringed) {
    ringed_ = ringed;
    Update();
    SchedulePaint();
  }

  // While the sidebar slides, the switcher draws the current fill instead.
  void SetFillMoved(bool moved) {
    if (fill_moved_ != moved) {
      fill_moved_ = moved;
      SchedulePaint();
    }
  }

  // views::Button:
  void PaintButtonContents(gfx::Canvas* canvas) override {
    const ui::ColorProvider* colors = GetColorProvider();
    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    const gfx::RectF bounds(GetLocalBounds());
    if (dot_) {
      flags.setColor(SkColorSetA(
          colors->GetColor(kColorTabForegroundInactiveFrameActive), kDotAlpha));
      canvas->DrawCircle(bounds.CenterPoint(), kDotSize / 2.0f, flags);
      return;
    }
    const SkColor fill =
        colors->GetColor(kColorNewTabButtonCRBackgroundFrameActive);
    if (current_) {
      if (!fill_moved_) {
        flags.setColor(fill);
        canvas->DrawRoundRect(bounds, kRadius, flags);
      }
    } else if (GetState() == STATE_HOVERED || GetState() == STATE_PRESSED) {
      flags.setColor(SkColorSetA(
          fill, static_cast<SkAlpha>(SkColorGetA(fill) * kHoverFill)));
      canvas->DrawRoundRect(bounds, kRadius, flags);
    }
    if (ringed_) {
      flags.setStyle(cc::PaintFlags::kStroke_Style);
      flags.setStrokeWidth(kRingWidth);
      flags.setColor(colors->GetColor(kColorMenuCardAccent));
      gfx::RectF ring = bounds;
      ring.Inset(kRingWidth / 2.0f);
      canvas->DrawRoundRect(ring, kRadius - kRingWidth / 2.0f, flags);
    }
  }

  void StateChanged(ButtonState old_state) override {
    Update();
    SchedulePaint();
  }

  // The name, then the key, looked up once the window is built.
  std::u16string GetRenderedTooltipText(
      const gfx::Point& point) const override {
    std::u16string tooltip = name_;
    ui::Accelerator key;
    if (index_ < 9 &&
        GetCommandAccelerator(window_, kGoToWorkspace1 + index_, &key)) {
      tooltip += u"  " + key.GetShortcutText();
    }
    return tooltip;
  }

 private:
  // The others are dimmed and a little gray until the pointer is on them.
  void Update() {
    const bool lit = current_ || ringed_ || GetState() == STATE_HOVERED ||
                     GetState() == STATE_PRESSED;
    layer()->SetOpacity(lit ? 1.0f : kDimOpacity);
    layer()->SetLayerGrayscale(lit || dot_ ? 0.0f : kDimGrayscale);
  }

  const raw_ptr<BrowserWindowInterface> window_;
  const std::string id_;
  const std::u16string name_;
  const int index_;
  const bool current_;
  const bool dot_;
  bool ringed_ = false;
  bool fill_moved_ = false;
};

BEGIN_METADATA(WorkspaceIcon)
END_METADATA

}  // namespace

DEFINE_USER_DATA(WorkspaceSwitcher);

// static
std::unique_ptr<WorkspaceSwitcher> WorkspaceSwitcher::Make(
    BrowserWindowInterface* window) {
  return WorkspaceWindow::From(window)
             ? std::make_unique<WorkspaceSwitcher>(window)
             : nullptr;
}

// static
WorkspaceSwitcher* WorkspaceSwitcher::From(BrowserWindowInterface* window) {
  return window ? Get(window->GetUnownedUserDataHost()) : nullptr;
}

WorkspaceSwitcher::WorkspaceSwitcher(BrowserWindowInterface* window)
    : window_(window), user_data_(window->GetUnownedUserDataHost(), *this) {
  auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal, gfx::Insets(), kSpacing));
  layout->set_main_axis_alignment(views::BoxLayout::MainAxisAlignment::kCenter);
  layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);
  GetViewAccessibility().SetRole(ax::mojom::Role::kGroup);
  GetViewAccessibility().SetName(l10n_util::GetStringUTF16(IDS_UW_WORKSPACES));
  list_subscription_ = WorkspaceList::Get().Observe(
      base::BindRepeating(&WorkspaceSwitcher::Rebuild, base::Unretained(this)));
  window_subscription_ = WorkspaceWindow::From(window_)->ObserveChanges(
      base::BindRepeating(&WorkspaceSwitcher::Rebuild, base::Unretained(this)));
  Rebuild();
}

WorkspaceSwitcher::~WorkspaceSwitcher() {
  swipe_.reset();
  card_.reset();
}

void WorkspaceSwitcher::SetSlide(const std::string& to, float amount) {
  slide_to_ = to;
  slide_amount_ = amount;
  const WorkspaceWindow* shown = WorkspaceWindow::From(window_);
  if (auto* current = views::AsViewClass<WorkspaceIcon>(
          shown ? IconFor(shown->workspace()) : nullptr)) {
    current->SetFillMoved(!to.empty() && IconFor(to));
  }
  SchedulePaint();
}

void WorkspaceSwitcher::AddedToWidget() {
  // The sidebar that holds the switcher is the one that swipes.
  swipe_.reset();
  BrowserView* browser_view = nullptr;
  VerticalTabStripRegionView* region = nullptr;
  for (views::View* view = parent(); view; view = view->parent()) {
    if (!region) {
      region = views::AsViewClass<VerticalTabStripRegionView>(view);
    }
    browser_view = views::AsViewClass<BrowserView>(view);
    if (browser_view) {
      break;
    }
  }
  if (region && browser_view) {
    swipe_ = std::make_unique<WorkspaceSwipe>(window_, browser_view, region);
  }
}

void WorkspaceSwitcher::OnPaint(gfx::Canvas* canvas) {
  views::View::OnPaint(canvas);
  const WorkspaceWindow* shown = WorkspaceWindow::From(window_);
  views::View* from = shown ? IconFor(shown->workspace()) : nullptr;
  views::View* to = slide_to_.empty() ? nullptr : IconFor(slide_to_);
  if (!from || !to) {
    return;
  }
  // The current fill glides toward the other workspace's icon, under the
  // icons, which draw on their own layers.
  const gfx::RectF a(from->bounds());
  const gfx::RectF b(to->bounds());
  const auto between = [this](float start, float end) {
    return gfx::Tween::FloatValueBetween(slide_amount_, start, end);
  };
  cc::PaintFlags flags;
  flags.setAntiAlias(true);
  flags.setColor(
      GetColorProvider()->GetColor(kColorNewTabButtonCRBackgroundFrameActive));
  canvas->DrawRoundRect(
      gfx::RectF(between(a.x(), b.x()), between(a.y(), b.y()),
                 between(a.width(), b.width()),
                 between(a.height(), b.height())),
      kRadius, flags);
}

void WorkspaceSwitcher::ShowNewWorkspaceCard() {
  ShowCard(
      MakeNewWorkspaceCard(this, parent() ? parent() : this, CardClosing()),
      std::string());
}

void WorkspaceSwitcher::ShowMenu(const std::string& id) {
  if (views::Button* icon = IconFor(id)) {
    ShowCard(MakeWorkspaceMenu(this, id, icon, CardClosing()), id);
  }
}

void WorkspaceSwitcher::ShowIconCard(const std::string& id) {
  if (views::Button* icon = IconFor(id)) {
    ShowCard(MakeIconCard(this, id, icon, CardClosing()), id);
  }
}

void WorkspaceSwitcher::ShowDeleteCard(const std::string& id) {
  ShowCard(MakeDeleteCard(this, id, CardClosing()), id);
}

views::Button* WorkspaceSwitcher::IconFor(std::string_view id) {
  for (views::View* child : children()) {
    if (auto* icon = views::AsViewClass<WorkspaceIcon>(child);
        icon && icon->id() == id) {
      return icon;
    }
  }
  return nullptr;
}

void WorkspaceSwitcher::ShowContextMenuForViewImpl(
    views::View* source,
    const gfx::Point& point,
    ui::mojom::MenuSourceType source_type) {
  if (auto* icon = views::AsViewClass<WorkspaceIcon>(source)) {
    ShowMenu(icon->id());
  }
}

void WorkspaceSwitcher::Rebuild() {
  // A card can point at an icon that goes, so the open one closes.
  card_.reset();
  ringed_.clear();
  hover_lock_.reset();
  RemoveAllChildViews();
  const WorkspaceWindow* shown = WorkspaceWindow::From(window_);
  const std::vector<WorkspaceList::Workspace>& workspaces =
      WorkspaceList::Get().workspaces();
  const bool dots = workspaces.size() > kMostIcons;
  for (size_t i = 0; i < workspaces.size(); ++i) {
    const WorkspaceList::Workspace& workspace = workspaces[i];
    const bool current = shown && workspace.id == shown->workspace();
    auto* icon = AddChildView(std::make_unique<WorkspaceIcon>(
        base::BindRepeating(
            [](BrowserWindowInterface* window, const std::string& id) {
              if (WorkspaceWindow* shown = WorkspaceWindow::From(window)) {
                shown->Slide(id);
              }
            },
            window_, workspace.id),
        window_, workspace, static_cast<int>(i), current, dots && !current));
    icon->set_context_menu_controller(this);
  }
  PreferredSizeChanged();
}

void WorkspaceSwitcher::ShowCard(CardBubble* bubble,
                                 const std::string& ringed) {
  card_.reset();
  if (auto* icon = views::AsViewClass<WorkspaceIcon>(IconFor(ringed_))) {
    icon->SetRinged(false);
  }
  card_ = views::BubbleDialogDelegate::CreateBubble(bubble);
  bubble->Watch(card_.get());
  ringed_ = ringed;
  if (auto* icon = views::AsViewClass<WorkspaceIcon>(IconFor(ringed_))) {
    icon->SetRinged(true);
  }
  if (BrowserView* view = BrowserView::GetBrowserViewForBrowser(window_)) {
    hover_lock_ = view->tab_strip_view()->GetExpandOnHoverLock(
        ExpandOnHoverLockType::kKeepCurrentState);
  }
  card_->Show();
}

void WorkspaceSwitcher::OnCardClosing() {
  if (!card_ || !card_->IsClosed()) {
    return;
  }
  card_.reset();
  hover_lock_.reset();
  if (auto* icon = views::AsViewClass<WorkspaceIcon>(IconFor(ringed_))) {
    icon->SetRinged(false);
  }
  ringed_.clear();
}

base::OnceClosure WorkspaceSwitcher::CardClosing() {
  return base::BindOnce(&WorkspaceSwitcher::OnCardClosing,
                        weak_factory_.GetWeakPtr());
}

BEGIN_METADATA(WorkspaceSwitcher)
END_METADATA

}  // namespace uw
