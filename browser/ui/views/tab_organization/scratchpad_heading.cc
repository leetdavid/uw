// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/browser/ui/views/tab_organization/scratchpad_heading.h"

#include <algorithm>
#include <memory>
#include <utility>

#include "base/callback_list.h"
#include "base/functional/bind.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/color/chrome_color_id.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/tabs/common/tab_group_view.h"
#include "chrome/grit/generated_resources.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/models/image_model.h"
#include "ui/color/color_provider.h"
#include "ui/gfx/font_list.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/size.h"
#include "ui/views/background.h"
#include "ui/views/border.h"
#include "ui/views/controls/button/label_button.h"
#include "ui/views/controls/label.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/view_utils.h"
#include "ui/views/widget/widget.h"
#include "uw/browser/tab_organization/tab_organization_controller.h"
#include "uw/browser/ui/vector_icons/vector_icons.h"

namespace uw {
namespace {

constexpr int kLabelInset = 8;
constexpr SkAlpha kLabelAlpha = 0x8C;
constexpr SkAlpha kButtonAlpha = 0xC7;
constexpr int kButtonHeight = 22;
constexpr int kButtonRadius = 7;
constexpr int kIconSize = 12;
constexpr int kIconGap = 4;

// Clear's small icon and word. The tint's fill shows under the pointer.
class ClearButton : public views::LabelButton {
  METADATA_HEADER(ClearButton, views::LabelButton)

 public:
  explicit ClearButton(PressedCallback callback)
      : LabelButton(std::move(callback),
                    l10n_util::GetStringUTF16(IDS_UW_SCRATCHPAD_CLEAR)) {
    label()->SetFontList(
        gfx::FontList().DeriveWithSizeDelta(-2).DeriveWithWeight(
            gfx::Font::Weight::MEDIUM));
    SetTextSubpixelRenderingEnabled(false);
    SetImageLabelSpacing(kIconGap);
    SetBorder(views::CreateEmptyBorder(gfx::Insets::TLBR(0, 6, 0, 7)));
    SetMinSize(gfx::Size(0, kButtonHeight));
    SetVisible(false);
    hovered_ = AddStateChangedCallback(
        base::BindRepeating(&ClearButton::UpdateFill, base::Unretained(this)));
  }

 private:
  void UpdateFill() {
    SetBackground(
        GetState() == STATE_NORMAL
            ? nullptr
            : views::CreateRoundedRectBackground(
                  kColorNewTabButtonCRBackgroundFrameActive, kButtonRadius));
  }

  base::CallbackListSubscription hovered_;
};

BEGIN_METADATA(ClearButton)
END_METADATA

}  // namespace

const views::View* FirstScratchpadTab(
    const std::vector<views::View*>& children) {
  const auto first = std::ranges::find_if_not(children, [](views::View* child) {
    return views::IsViewClass<TabGroupView>(child);
  });
  return first == children.end() ? nullptr : *first;
}

ScratchpadHeading::ScratchpadHeading() {
  auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal,
      gfx::Insets::TLBR(0, kLabelInset, 0, 0)));
  layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);

  label_ = AddChildView(std::make_unique<views::Label>(
      l10n_util::GetStringUTF16(IDS_UW_SCRATCHPAD)));
  label_->SetFontList(gfx::FontList().DeriveWithSizeDelta(-1).DeriveWithWeight(
      gfx::Font::Weight::SEMIBOLD));
  label_->SetHorizontalAlignment(gfx::ALIGN_TO_HEAD);
  label_->SetBackgroundColor(SK_ColorTRANSPARENT);
  label_->SetSubpixelRenderingEnabled(false);
  layout->SetFlexForView(label_, 1);

  clear_ = AddChildView(std::make_unique<ClearButton>(
      base::BindRepeating(&ScratchpadHeading::Clear, base::Unretained(this))));

  SetNotifyEnterExitOnChild(true);
}

ScratchpadHeading::~ScratchpadHeading() = default;

void ScratchpadHeading::OnMouseEntered(const ui::MouseEvent& event) {
  if (clear_->GetVisible()) {
    return;
  }
  clear_->SetVisible(true);
  if (const TabOrganizationController* controller = Controller()) {
    controller->FindClearable(base::BindOnce(&ScratchpadHeading::ShowCount,
                                             weak_factory_.GetWeakPtr()));
  }
}

void ScratchpadHeading::OnMouseExited(const ui::MouseEvent& event) {
  if (!IsMouseHovered()) {
    clear_->SetVisible(false);
  }
}

void ScratchpadHeading::OnThemeChanged() {
  views::View::OnThemeChanged();
  const SkColor color =
      GetColorProvider()->GetColor(kColorTabForegroundInactiveFrameActive);
  const SkColor resting = SkColorSetA(color, kButtonAlpha);
  label_->SetEnabledColor(SkColorSetA(color, kLabelAlpha));
  clear_->SetTextColor(views::Button::STATE_NORMAL, resting);
  clear_->SetTextColor(views::Button::STATE_HOVERED, color);
  clear_->SetTextColor(views::Button::STATE_PRESSED, color);
  clear_->SetImageModel(
      views::Button::STATE_NORMAL,
      ui::ImageModel::FromVectorIcon(kArrowDownIcon, resting, kIconSize));
  const ui::ImageModel hot =
      ui::ImageModel::FromVectorIcon(kArrowDownIcon, color, kIconSize);
  clear_->SetImageModel(views::Button::STATE_HOVERED, hot);
  clear_->SetImageModel(views::Button::STATE_PRESSED, hot);
}

TabOrganizationController* ScratchpadHeading::Controller() const {
  const views::Widget* widget = GetWidget();
  BrowserView* window =
      widget ? BrowserView::GetBrowserViewForNativeWindow(
                   widget->GetPrimaryWindowWidget()->GetNativeWindow())
             : nullptr;
  return window ? TabOrganizationController::From(window->browser()) : nullptr;
}

void ScratchpadHeading::Clear() {
  if (TabOrganizationController* controller = Controller()) {
    controller->ClearScratchpad();
  }
}

void ScratchpadHeading::ShowCount(std::vector<PageId> clearable) {
  clear_->SetTooltipText(l10n_util::GetPluralStringFUTF16(
      IDS_UW_SCRATCHPAD_CLEAR_TOOLTIP, static_cast<int>(clearable.size())));
}

BEGIN_METADATA(ScratchpadHeading)
END_METADATA

}  // namespace uw
