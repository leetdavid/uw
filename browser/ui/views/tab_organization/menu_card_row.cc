// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/browser/ui/views/tab_organization/menu_card_row.h"

#include <utility>

#include "ui/base/accelerators/accelerator.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/models/image_model.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/vector_icon_types.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/background.h"
#include "ui/views/border.h"
#include "ui/views/bubble/bubble_dialog_delegate_view.h"
#include "ui/views/controls/image_view.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/separator.h"
#include "ui/views/focus/focus_manager.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/view_class_properties.h"
#include "uw/browser/ui/look/menu_card.h"

namespace uw {
namespace {

constexpr int kIconSize = 16;
constexpr int kGap = 10;
constexpr int kSideInset = 10;
constexpr int kTwoLineInset = 7;
constexpr int kChipHeight = 24;
constexpr int kChipInset = 8;
constexpr int kSeparatorInset = 4;

// The hover fill follows the pointer, and the keyboard once the arrow keys or
// Tab moved into the card. A row that took focus as the card opened is not
// filled, as a macOS menu shows nothing until a key is pressed.
bool ShowsFill(const views::Button* button) {
  if (button->GetState() == views::Button::STATE_HOVERED ||
      button->GetState() == views::Button::STATE_PRESSED) {
    return true;
  }
  const views::FocusManager* focus = button->GetFocusManager();
  return button->HasFocus() && focus &&
         focus->focus_change_reason() ==
             views::FocusManager::FocusChangeReason::kFocusTraversal;
}

std::unique_ptr<views::BoxLayout> RowLayout(views::View* row) {
  auto layout = std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal,
      gfx::Insets::VH(0, kSideInset), kGap);
  layout->set_minimum_cross_axis_size(kMenuRowHeight);
  layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);
  return layout;
}

}  // namespace

MenuCardRow::MenuCardRow(PressedCallback callback,
                         const gfx::VectorIcon& icon,
                         const std::u16string& text)
    : Button(std::move(callback)), icon_(icon) {
  views::BoxLayout* layout = SetLayoutManager(RowLayout(this));
  SetInstallFocusRingOnFocus(false);
  image_ = AddChildView(std::make_unique<views::ImageView>());
  column_ = AddChildView(std::make_unique<views::View>());
  column_->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical, gfx::Insets(), 2));
  layout->SetFlexForView(column_, 1);
  line_ = column_->AddChildView(std::make_unique<views::View>());
  auto* line_layout =
      line_->SetLayoutManager(std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kHorizontal));
  line_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);
  label_ = line_->AddChildView(std::make_unique<views::Label>(text));
  label_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  line_layout->SetFlexForView(label_, 1);
  GetViewAccessibility().SetName(text);
  Update();
}

MenuCardRow::~MenuCardRow() = default;

void MenuCardRow::SetEnd(std::unique_ptr<views::View> view) {
  line_->AddChildView(std::move(view));
}

void MenuCardRow::SetDetail(std::unique_ptr<views::View> detail) {
  column_->AddChildView(std::move(detail));
  auto* layout = static_cast<views::BoxLayout*>(GetLayoutManager());
  layout->set_inside_border_insets(gfx::Insets::VH(kTwoLineInset, kSideInset));
  layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStart);
}

void MenuCardRow::StateChanged(ButtonState old_state) {
  Button::StateChanged(old_state);
  Update();
}

void MenuCardRow::OnFocus() {
  Button::OnFocus();
  Update();
}

void MenuCardRow::OnBlur() {
  Button::OnBlur();
  Update();
}

void MenuCardRow::Update() {
  const bool enabled = GetEnabled();
  image_->SetImage(ui::ImageModel::FromVectorIcon(
      *icon_, enabled ? kColorMenuCardIcon : kColorMenuCardDisabled,
      kIconSize));
  label_->SetEnabledColor(enabled ? kColorMenuCardText
                                  : kColorMenuCardDisabled);
  SetBackground(ShowsFill(this) ? views::CreateRoundedRectBackground(
                                      kColorMenuCardHover, kMenuRowRadius)
                                : nullptr);
}

MenuCardChip::MenuCardChip(PressedCallback callback, const std::u16string& text)
    : LabelButton(std::move(callback), text) {
  SetInstallFocusRingOnFocus(false);
  SetBorder(views::CreateEmptyBorder(gfx::Insets::VH(0, kChipInset)));
  SetHorizontalAlignment(gfx::ALIGN_CENTER);
  label()->SetFontList(label()->font_list().DeriveWithSizeDelta(-1));
  SetMinSize(gfx::Size(0, kChipHeight));
  SetMaxSize(gfx::Size(0, kChipHeight));
  SetTextColor(STATE_NORMAL, kColorMenuCardText);
  SetTextColor(STATE_HOVERED, kColorMenuCardText);
  SetTextColor(STATE_PRESSED, kColorMenuCardText);
  Update();
}

MenuCardChip::~MenuCardChip() = default;

void MenuCardChip::StateChanged(ButtonState old_state) {
  LabelButton::StateChanged(old_state);
  Update();
}

void MenuCardChip::OnFocus() {
  LabelButton::OnFocus();
  Update();
}

void MenuCardChip::OnBlur() {
  LabelButton::OnBlur();
  Update();
}

void MenuCardChip::Update() {
  SetBackground(views::CreateRoundedRectBackground(
      ShowsFill(this) ? kColorMenuCardChipHover : kColorMenuCardHover,
      kChipHeight / 2));
}

std::unique_ptr<views::View> MakeMenuCardHeader(const gfx::VectorIcon& icon,
                                                const std::u16string& text) {
  auto header = std::make_unique<views::View>();
  header->SetLayoutManager(RowLayout(header.get()));
  header->AddChildView(std::make_unique<views::ImageView>(
      ui::ImageModel::FromVectorIcon(icon, kColorMenuCardIcon, kIconSize)));
  views::Label* label =
      header->AddChildView(std::make_unique<views::Label>(text));
  label->SetEnabledColor(kColorMenuCardText);
  return header;
}

std::unique_ptr<views::View> MakeMenuCardSeparator() {
  auto separator = std::make_unique<views::Separator>();
  separator->SetColorId(kColorMenuCardLine);
  separator->SetProperty(views::kMarginsKey,
                         gfx::Insets::VH(kMenuCardPadding, kSeparatorInset));
  return separator;
}

std::unique_ptr<views::Label> MakeMenuCardKey(
    const ui::Accelerator& accelerator) {
  auto key = MakeMenuCardDetail(accelerator.GetShortcutText());
  key->SetProperty(views::kMarginsKey, gfx::Insets::TLBR(0, kGap, 0, 0));
  return key;
}

std::unique_ptr<views::Label> MakeMenuCardDetail(const std::u16string& text) {
  auto detail = std::make_unique<views::Label>(text);
  detail->SetFontList(detail->font_list().DeriveWithSizeDelta(-1));
  detail->SetEnabledColor(kColorMenuCardMuted);
  detail->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  detail->SetElideBehavior(gfx::ELIDE_TAIL);
  return detail;
}

void SetUpMenuCard(views::BubbleDialogDelegate* bubble) {
  bubble->SetButtons(0);
  bubble->SetShowCloseButton(false);
  bubble->SetShowTitle(false);
  bubble->set_margins(gfx::Insets(kMenuCardPadding));
  bubble->set_corner_radius(kMenuCardRadius);
  bubble->set_fixed_width(kMenuCardWidth);
  bubble->SetBackgroundColor(kColorMenuCardBackground);
  bubble->SetEnableArrowKeyTraversal(true);
}

void LayOutMenuCard(views::View* contents) {
  auto* layout = contents->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical));
  layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);
}

BEGIN_METADATA(MenuCardRow)
END_METADATA

BEGIN_METADATA(MenuCardChip)
END_METADATA

}  // namespace uw
