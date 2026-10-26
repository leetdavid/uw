// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/browser/ui/views/tab_organization/scratchpad_heading.h"

#include <algorithm>
#include <memory>
#include <utility>

#include "base/callback_list.h"
#include "base/functional/bind.h"
#include "base/memory/raw_ref.h"
#include "chrome/browser/profiles/profile.h"
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
#include "uw/browser/tab_organization/automatic_organizer.h"
#include "uw/browser/tab_organization/commands.h"
#include "uw/browser/tab_organization/tab_organization_controller.h"
#include "uw/browser/ui/vector_icons/vector_icons.h"
#include "uw/browser/ui/views/tab_organization/tint_style.h"
#include "uw/browser/ui/views/tab_organization/window_commands.h"

namespace uw {
namespace {

constexpr int kLabelInset = 8;
constexpr SkAlpha kLabelAlpha = 0x8C;
constexpr SkAlpha kButtonAlpha = 0xC7;
constexpr SkAlpha kGrayedAlpha = 0x52;
constexpr int kButtonHeight = 22;
constexpr int kButtonRadius = 7;
constexpr int kIconSize = 12;
constexpr int kIconGap = 4;

// A small icon and word on the heading. The tint's fill shows under the
// pointer.
class HeadingButton : public views::LabelButton {
  METADATA_HEADER(HeadingButton, views::LabelButton)

 public:
  HeadingButton(PressedCallback callback, int text, const gfx::VectorIcon& icon)
      : LabelButton(std::move(callback), l10n_util::GetStringUTF16(text)),
        icon_(icon) {
    label()->SetFontList(
        gfx::FontList().DeriveWithSizeDelta(-2).DeriveWithWeight(
            gfx::Font::Weight::MEDIUM));
    SetTextSubpixelRenderingEnabled(false);
    SetImageLabelSpacing(kIconGap);
    SetBorder(views::CreateEmptyBorder(gfx::Insets::TLBR(0, 6, 0, 7)));
    SetMinSize(gfx::Size(0, kButtonHeight));
    SetVisible(false);
    hovered_ = AddStateChangedCallback(base::BindRepeating(
        &HeadingButton::UpdateFill, base::Unretained(this)));
  }

  // views::LabelButton:
  void OnThemeChanged() override {
    LabelButton::OnThemeChanged();
    const SkColor color =
        GetColorProvider()->GetColor(kColorTabForegroundInactiveFrameActive);
    const SkColor resting = SkColorSetA(color, kButtonAlpha);
    const SkColor grayed = SkColorSetA(color, kGrayedAlpha);
    SetTextColor(STATE_NORMAL, resting);
    SetTextColor(STATE_HOVERED, color);
    SetTextColor(STATE_PRESSED, color);
    SetTextColor(STATE_DISABLED, grayed);
    SetImageModel(STATE_NORMAL,
                  ui::ImageModel::FromVectorIcon(*icon_, resting, kIconSize));
    const ui::ImageModel hot =
        ui::ImageModel::FromVectorIcon(*icon_, color, kIconSize);
    SetImageModel(STATE_HOVERED, hot);
    SetImageModel(STATE_PRESSED, hot);
    SetImageModel(STATE_DISABLED,
                  ui::ImageModel::FromVectorIcon(*icon_, grayed, kIconSize));
  }

 private:
  void UpdateFill() {
    SetBackground(GetState() == STATE_HOVERED || GetState() == STATE_PRESSED
                      ? CreateGlassPillBackground(
                            GlassOfView(this),
                            kColorNewTabButtonCRBackgroundFrameActive,
                            kButtonRadius)
                      : nullptr);
  }

  const raw_ref<const gfx::VectorIcon> icon_;
  base::CallbackListSubscription hovered_;
};

BEGIN_METADATA(HeadingButton)
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

  tidy_ = AddChildView(std::make_unique<HeadingButton>(
      base::BindRepeating(&ScratchpadHeading::Tidy, base::Unretained(this)),
      IDS_UW_SCRATCHPAD_TIDY, kSparklesIcon));
  clear_ = AddChildView(std::make_unique<HeadingButton>(
      base::BindRepeating(&ScratchpadHeading::Clear, base::Unretained(this)),
      IDS_UW_SCRATCHPAD_CLEAR, kArrowDownIcon));

  SetNotifyEnterExitOnChild(true);
}

ScratchpadHeading::~ScratchpadHeading() = default;

void ScratchpadHeading::OnMouseEntered(const ui::MouseEvent& event) {
  if (clear_->GetVisible()) {
    return;
  }
  const Browser* window = Window();
  tidy_->SetVisible(window && !window->GetProfile()->IsOffTheRecord());
  UpdateTidy();
  clear_->SetVisible(true);
  if (const TabOrganizationController* controller = Controller()) {
    controller->FindClearable(base::BindOnce(&ScratchpadHeading::ShowCount,
                                             weak_factory_.GetWeakPtr()));
  }
}

void ScratchpadHeading::OnMouseExited(const ui::MouseEvent& event) {
  if (!IsMouseHovered()) {
    tidy_->SetVisible(false);
    clear_->SetVisible(false);
  }
}

void ScratchpadHeading::OnThemeChanged() {
  views::View::OnThemeChanged();
  label_->SetEnabledColor(SkColorSetA(
      GetColorProvider()->GetColor(kColorTabForegroundInactiveFrameActive),
      kLabelAlpha));
}

void ScratchpadHeading::AddedToWidget() {
  views::View::AddedToWidget();
  if (AutomaticOrganizer* organizer = Organizer()) {
    organizer_events_ = organizer->ObserveEvents(base::BindRepeating(
        [](ScratchpadHeading* heading, const AutomaticOrganizer::Event&) {
          heading->UpdateTidy();
        },
        base::Unretained(this)));
  }
}

void ScratchpadHeading::RemovedFromWidget() {
  organizer_events_ = {};
  views::View::RemovedFromWidget();
}

Browser* ScratchpadHeading::Window() const {
  const views::Widget* widget = GetWidget();
  BrowserView* window =
      widget ? BrowserView::GetBrowserViewForNativeWindow(
                   widget->GetPrimaryWindowWidget()->GetNativeWindow())
             : nullptr;
  return window ? window->browser() : nullptr;
}

TabOrganizationController* ScratchpadHeading::Controller() const {
  Browser* window = Window();
  return window ? TabOrganizationController::From(window) : nullptr;
}

AutomaticOrganizer* ScratchpadHeading::Organizer() const {
  Browser* window = Window();
  return window ? AutomaticOrganizer::From(window) : nullptr;
}

void ScratchpadHeading::UpdateTidy() {
  const AutomaticOrganizer* organizer = Organizer();
  tidy_->SetEnabled(organizer && !organizer->IsTidying());
  tidy_->SetTooltipText(l10n_util::GetStringUTF16(
      organizer && organizer->CanTidy() ? IDS_UW_SCRATCHPAD_TIDY_TOOLTIP
                                        : IDS_UW_SCRATCHPAD_TIDY_NEEDS_MODEL));
}

void ScratchpadHeading::Tidy() {
  if (Browser* window = Window()) {
    RunWindowCommand(window, kTidyScratchpad);
  }
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
