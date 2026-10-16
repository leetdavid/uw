// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/browser/ui/views/tab_organization/suggest_name_item.h"

#include <memory>

#include "base/functional/bind.h"
#include "chrome/browser/ui/views/bubble_menu_item_factory.h"
#include "chrome/browser/ui/views/controls/hover_button.h"
#include "chrome/grit/generated_resources.h"
#include "components/vector_icons/vector_icons.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/models/image_model.h"
#include "ui/color/color_id.h"
#include "ui/views/border.h"
#include "ui/views/controls/button/label_button.h"
#include "ui/views/style/typography.h"
#include "uw/browser/tab_organization/automatic_organizer.h"

namespace uw {
namespace {

constexpr int kIconSize = 20;

}  // namespace

SuggestNameItem::SuggestNameItem(BrowserWindowInterface* window,
                                 const tab_groups::TabGroupId& group,
                                 const gfx::Insets& insets)
    : group_(group) {
  AutomaticOrganizer* organizer = AutomaticOrganizer::From(window);
  if (!organizer || !organizer->CanSuggestName()) {
    SetVisible(false);
    return;
  }
  organizer_ = organizer->AsWeakPtr();
  SetUseDefaultFillLayout(true);
  button_ = AddChildView(CreateBubbleMenuItem(
      0, l10n_util::GetStringUTF16(IDS_UW_SUGGEST_NAME_MAC),
      base::BindRepeating(&SuggestNameItem::Suggest, base::Unretained(this)),
      ui::ImageModel::FromVectorIcon(vector_icons::kButtonMagicIcon,
                                     ui::kColorMenuIcon, kIconSize)));
  button_->SetImageModel(
      views::Button::STATE_DISABLED,
      ui::ImageModel::FromVectorIcon(vector_icons::kButtonMagicIcon,
                                     ui::kColorMenuIconDisabled, kIconSize));
  button_->SetBorder(views::CreateEmptyBorder(insets));
  button_->SetLabelStyle(views::style::STYLE_BODY_3_EMPHASIS);
  naming_ = organizer->ObserveNaming(
      base::BindRepeating(&SuggestNameItem::Refresh, base::Unretained(this)));
  Refresh();
}

SuggestNameItem::~SuggestNameItem() = default;

void SuggestNameItem::Suggest() {
  if (!organizer_) {
    return;
  }
  failed_ = false;
  reason_.clear();
  organizer_->SuggestName(
      group_.ToString(),
      base::BindOnce(&SuggestNameItem::OnFailed, weak_factory_.GetWeakPtr()));
  Refresh();
}

void SuggestNameItem::OnFailed(const std::u16string& reason) {
  failed_ = true;
  reason_ = reason;
  Refresh();
}

void SuggestNameItem::Refresh() {
  const bool naming = organizer_ && organizer_->IsNaming(group_.ToString());
  button_->SetText(l10n_util::GetStringUTF16(naming ? IDS_UW_SUGGESTING_NAME
                                             : failed_
                                                 ? IDS_UW_SUGGEST_NAME_FAILED
                                                 : IDS_UW_SUGGEST_NAME_MAC));
  button_->SetTooltipText(naming ? std::u16string() : reason_);
  button_->SetEnabled(!naming);
}

BEGIN_METADATA(SuggestNameItem)
END_METADATA

}  // namespace uw
