// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/browser/ui/views/tab_organization/branch_toggle.h"

#include <algorithm>
#include <memory>
#include <optional>
#include <string>

#include "base/functional/bind.h"
#include "base/strings/string_number_conversions.h"
#include "chrome/browser/ui/color/chrome_color_id.h"
#include "chrome/grit/generated_resources.h"
#include "components/vector_icons/vector_icons.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/models/image_model.h"
#include "ui/color/color_provider.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/border.h"
#include "ui/views/layout/proposed_layout.h"
#include "ui/views/view_utils.h"
#include "uw/browser/tab_organization/tab_organization_controller.h"
#include "uw/browser/ui/views/tab_organization/chat_mark.h"
#include "uw/browser/ui/views/tab_organization/kept_mark.h"
#include "uw/browser/ui/views/tab_organization/key_hint.h"
#include "uw/browser/ui/views/tab_organization/split_mark.h"

namespace uw {
namespace {

constexpr int kIconSize = 16;
constexpr int kPadding = 2;
constexpr int kCountGap = 6;
constexpr SkAlpha kChevronAlpha = 0xBF;
constexpr SkAlpha kCountAlpha = 0x8C;

SkColor TextColor(const views::View* view) {
  return view->GetColorProvider()->GetColor(
      kColorTabForegroundInactiveFrameActive);
}

template <typename T>
T* ChildOfClass(views::View* row) {
  for (views::View* child : row->children()) {
    if (T* found = views::AsViewClass<T>(child)) {
      return found;
    }
  }
  return nullptr;
}

}  // namespace

BranchToggle::BranchToggle(tabs::TabHandle tab)
    : ImageButton(
          base::BindRepeating(&BranchToggle::Toggle, base::Unretained(this))),
      tab_(tab) {
  SetBorder(views::CreateEmptyBorder(gfx::Insets(kPadding)));
  SetImageHorizontalAlignment(ALIGN_CENTER);
  SetImageVerticalAlignment(ALIGN_MIDDLE);
  SetVisible(false);
}

BranchToggle::~BranchToggle() = default;

void BranchToggle::AddedToWidget() {
  ImageButton::AddedToWidget();
  if (TabOrganizationController* controller = Controller()) {
    changed_ = controller->Observe(
        base::BindRepeating(&BranchToggle::Refresh, base::Unretained(this)));
  }
  Refresh();
}

void BranchToggle::RemovedFromWidget() {
  changed_ = {};
  ImageButton::RemovedFromWidget();
}

void BranchToggle::OnThemeChanged() {
  ImageButton::OnThemeChanged();
  UpdateImage();
}

TabOrganizationController* BranchToggle::Controller() const {
  tabs::TabInterface* tab = tab_.Get();
  return tab ? TabOrganizationController::From(tab->GetBrowserWindowInterface())
             : nullptr;
}

void BranchToggle::Refresh() {
  const TabOrganizationController* controller = Controller();
  const std::optional<PageId> id =
      controller ? controller->IdFor(tab_.Get()) : std::nullopt;
  const int descendants =
      id ? static_cast<int>(controller->tree().BranchOf(*id).size()) - 1 : 0;
  const bool collapsed = id && controller->IsCollapsed(*id);
  if (descendants == descendants_ && collapsed == collapsed_) {
    return;
  }
  descendants_ = descendants;
  collapsed_ = collapsed;
  const std::u16string action = l10n_util::GetStringUTF16(
      collapsed_ ? IDS_UW_EXPAND_BRANCH : IDS_UW_COLLAPSE_BRANCH);
  SetTooltipText(action);
  GetViewAccessibility().SetName(action);
  if (GetColorProvider()) {
    UpdateImage();
  }
  if (HiddenCount* count = ChildOfClass<HiddenCount>(parent())) {
    count->SetCount(descendants_);
  }
  parent()->InvalidateLayout();
}

void BranchToggle::UpdateImage() {
  const gfx::VectorIcon& chevron =
      collapsed_ ? vector_icons::kKeyboardArrowRightFlippableIcon
                 : vector_icons::kKeyboardArrowDownIcon;
  const SkColor color = TextColor(this);
  SetImageModel(STATE_NORMAL,
                ui::ImageModel::FromVectorIcon(
                    chevron, SkColorSetA(color, kChevronAlpha), kIconSize));
  const ui::ImageModel hot =
      ui::ImageModel::FromVectorIcon(chevron, color, kIconSize);
  SetImageModel(STATE_HOVERED, hot);
  SetImageModel(STATE_PRESSED, hot);
}

void BranchToggle::Toggle() {
  TabOrganizationController* controller = Controller();
  const std::optional<PageId> id =
      controller ? controller->IdFor(tab_.Get()) : std::nullopt;
  if (id) {
    controller->ToggleBranches({*id});
  }
}

BEGIN_METADATA(BranchToggle)
END_METADATA

HiddenCount::HiddenCount() {
  SetFontList(font_list().DeriveWithSizeDelta(-2));
  SetSubpixelRenderingEnabled(false);
  SetAutoColorReadabilityEnabled(false);
  SetVisible(false);
}

HiddenCount::~HiddenCount() = default;

void HiddenCount::SetCount(int count) {
  SetText(base::NumberToString16(count));
  const std::u16string hint =
      l10n_util::GetPluralStringFUTF16(IDS_UW_HIDDEN_TABS, count);
  SetTooltipText(hint);
  GetViewAccessibility().SetName(hint);
}

void HiddenCount::SetWarning(bool warning) {
  if (warning == warning_) {
    return;
  }
  warning_ = warning;
  SetFontList(font_list().DeriveWithWeight(
      warning_ ? gfx::Font::Weight::SEMIBOLD : gfx::Font::Weight::NORMAL));
  if (GetWidget()) {
    OnThemeChanged();
  }
}

void HiddenCount::OnThemeChanged() {
  Label::OnThemeChanged();
  SetEnabledColor(warning_ ? CloseHintColor(this)
                           : SkColorSetA(TextColor(this), kCountAlpha));
}

BEGIN_METADATA(HiddenCount)
END_METADATA

void AddBranchControls(views::View* row,
                       tabs::TabHandle tab,
                       views::Button* close,
                       base::RepeatingClosure update_colors) {
  row->AddChildView(std::make_unique<HiddenCount>());
  row->AddChildView(std::make_unique<BranchToggle>(tab));
  row->AddChildView(std::make_unique<ChatMark>(tab));
  row->AddChildView(std::make_unique<KeptMark>(tab));
  row->AddChildView(std::make_unique<SplitMark>(tab));
  // Last, so its tint covers the row.
  row->AddChildView(
      std::make_unique<KeyHint>(tab, close, std::move(update_colors)));
}

void LayOutBranchControls(views::View* icon,
                          views::View* title,
                          bool hovered,
                          views::ProposedLayout& layout) {
  views::View* row = icon->parent();
  BranchToggle* toggle = ChildOfClass<BranchToggle>(row);
  HiddenCount* count = ChildOfClass<HiddenCount>(row);
  ChatMark* mark = ChildOfClass<ChatMark>(row);
  KeptMark* kept = ChildOfClass<KeptMark>(row);
  SplitMark* split = ChildOfClass<SplitMark>(row);
  KeyHint* hint = ChildOfClass<KeyHint>(row);
  if (!toggle || !count || !mark || !kept || !split || !hint) {
    return;
  }
  views::ChildLayout* icon_layout = layout.GetLayoutFor(icon);
  views::ChildLayout* title_layout = layout.GetLayoutFor(title);
  const bool titled = icon_layout && icon_layout->visible && title_layout &&
                      title_layout->visible;
  const bool branch = titled && toggle->descendants() > 0;

  gfx::Rect toggle_bounds(toggle->GetPreferredSize());
  if (icon_layout) {
    toggle_bounds.set_origin(icon_layout->bounds.CenterPoint() -
                             toggle_bounds.CenterPoint().OffsetFromOrigin());
  }
  const bool show_toggle = branch && hovered;
  if (show_toggle) {
    icon_layout->visible = false;
  }

  // A kept tab's anchor ends its row, after a chat's state and the hidden
  // count.
  gfx::Rect kept_bounds(kept->GetPreferredSize());
  const bool show_kept = titled && kept->kept();
  if (show_kept) {
    gfx::Rect& text = title_layout->bounds;
    kept_bounds.set_origin({text.right() - kept_bounds.width(),
                            text.CenterPoint().y() - kept_bounds.height() / 2});
    text.set_width(std::max(0, text.width() - kept_bounds.width()));
  }

  // The split icon of a side-by-side pair's top row comes before it.
  gfx::Rect split_bounds(split->GetPreferredSize());
  const bool show_split = titled && split->ShownOnRow(hovered);
  if (show_split) {
    gfx::Rect& text = title_layout->bounds;
    split_bounds.set_origin(
        {text.right() - split_bounds.width(),
         text.CenterPoint().y() - split_bounds.height() / 2});
    text.set_width(std::max(0, text.width() - split_bounds.width()));
  }

  // A chat's state comes next, after the hidden count.
  gfx::Rect mark_bounds(mark->GetPreferredSize());
  const bool show_mark = titled && mark->shown();
  if (show_mark) {
    gfx::Rect& text = title_layout->bounds;
    mark_bounds.set_origin({text.right() - mark_bounds.width(),
                            text.CenterPoint().y() - mark_bounds.height() / 2});
    text.set_width(std::max(0, text.width() - mark_bounds.width() - kCountGap));
  }

  gfx::Rect count_bounds(count->GetPreferredSize());
  const bool show_count = branch && toggle->collapsed();
  if (show_count) {
    gfx::Rect& text = title_layout->bounds;
    count_bounds.set_origin(
        {text.right() - count_bounds.width(),
         text.CenterPoint().y() - count_bounds.height() / 2});
    text.set_width(
        std::max(0, text.width() - count_bounds.width() - kCountGap));
  }

  // Appending can move the layouts that `icon_layout` and `title_layout`
  // point to, so it comes last.
  layout.child_layouts.push_back({toggle, show_toggle, toggle_bounds});
  layout.child_layouts.push_back({count, show_count, count_bounds});
  layout.child_layouts.push_back({mark, show_mark, mark_bounds});
  layout.child_layouts.push_back({kept, show_kept, kept_bounds});
  layout.child_layouts.push_back({split, show_split, split_bounds});
  layout.child_layouts.push_back({hint, true, gfx::Rect(layout.host_size)});
}

}  // namespace uw
