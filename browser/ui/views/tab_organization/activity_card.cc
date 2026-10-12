// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/browser/ui/views/tab_organization/activity_card.h"

#include <memory>
#include <string>
#include <utility>

#include "base/functional/bind.h"
#include "base/strings/stringprintf.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/ui/color/chrome_color_id.h"
#include "chrome/grit/generated_resources.h"
#include "components/vector_icons/vector_icons.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/models/image_model.h"
#include "ui/color/color_id.h"
#include "ui/gfx/font.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/size.h"
#include "ui/gfx/text_constants.h"
#include "ui/gfx/vector_icon_types.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/background.h"
#include "ui/views/controls/button/md_text_button.h"
#include "ui/views/controls/image_view.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/progress_bar.h"
#include "ui/views/controls/throbber.h"
#include "ui/views/layout/flex_layout.h"
#include "ui/views/view_class_properties.h"
#include "uw/browser/ui/views/tab_organization/tint_style.h"

namespace uw {
namespace {

using Event = AutomaticOrganizer::Event;
using Kind = AutomaticOrganizer::Event::Kind;

constexpr int kCardRadius = 10;
constexpr int kCardPadding = 10;
constexpr int kLineSpacing = 4;
constexpr int kIconSize = 14;
constexpr int kIconGap = 6;
constexpr int kMeterHeight = 4;
constexpr int kMeterSpacing = 7;
constexpr int kActionSpacing = 6;
constexpr gfx::Insets kActionPadding = gfx::Insets::VH(3, 10);
constexpr int kSmallerText = -1;
// What an indeterminate views::ProgressBar takes as its value.
constexpr double kStillWaiting = -1.0;

bool Waits(Kind kind) {
  return kind == Kind::kAsking || kind == Kind::kNaming;
}

bool OffersUndo(Kind kind) {
  return kind == Kind::kMoved || kind == Kind::kCreated;
}

// The kinds that carry the model's confidence in a choice.
bool HasConfidence(Kind kind) {
  return OffersUndo(kind) || kind == Kind::kLeft || kind == Kind::kUnsure;
}

const gfx::VectorIcon& IconFor(Kind kind) {
  switch (kind) {
    case Kind::kMoved:
    case Kind::kCreated:
      return vector_icons::kCheckCircleFilledIcon;
    case Kind::kUnsure:
      return vector_icons::kLightbulbIcon;
    case Kind::kRequestFailed:
    case Kind::kAnswerUnreadable:
      return vector_icons::kWarningIcon;
    case Kind::kAsking:
    case Kind::kNaming:
    case Kind::kLeft:
    case Kind::kDropped:
      return vector_icons::kInfoIcon;
  }
}

std::u16string Headline(const Event& event) {
  switch (event.kind) {
    case Kind::kAsking:
      return l10n_util::GetStringFUTF16(IDS_UW_ACTIVITY_ASKING, event.model);
    case Kind::kNaming:
      return l10n_util::GetStringFUTF16(IDS_UW_ACTIVITY_NAMING, event.detail);
    case Kind::kMoved:
      return l10n_util::GetStringFUTF16(IDS_UW_AUTO_MOVED_TASK, event.detail);
    case Kind::kCreated:
      return l10n_util::GetStringFUTF16(IDS_UW_AUTO_CREATED_TASK, event.detail);
    case Kind::kLeft:
      return l10n_util::GetStringUTF16(IDS_UW_ACTIVITY_LEFT);
    case Kind::kUnsure:
      return l10n_util::GetStringFUTF16(IDS_UW_ACTIVITY_UNSURE, event.detail);
    case Kind::kRequestFailed:
      return l10n_util::GetStringFUTF16(IDS_UW_ACTIVITY_REQUEST_FAILED,
                                        event.detail);
    case Kind::kAnswerUnreadable:
      return l10n_util::GetStringUTF16(IDS_UW_ACTIVITY_ANSWER_UNREADABLE);
    case Kind::kDropped:
      return l10n_util::GetStringUTF16(IDS_UW_ACTIVITY_DROPPED);
  }
}

std::u16string Number(double value, int decimals) {
  return base::UTF8ToUTF16(base::StringPrintf("%.*f", decimals, value));
}

// A row of the card that lays its children out side by side.
std::unique_ptr<views::FlexLayoutView> Row() {
  auto row = std::make_unique<views::FlexLayoutView>();
  row->SetCrossAxisAlignment(views::LayoutAlignment::kCenter);
  row->SetProperty(views::kMarginsKey,
                   gfx::Insets::TLBR(kLineSpacing, 0, 0, 0));
  return row;
}

// One line of text that gives way to its neighbors and ends in an ellipsis.
std::unique_ptr<views::Label> Line(int size_delta, gfx::Font::Weight weight) {
  auto label = std::make_unique<views::Label>();
  StyleOnTint(label.get());
  label->SetElideBehavior(gfx::ELIDE_TAIL);
  label->SetFontList(label->font_list()
                         .DeriveWithSizeDelta(size_delta)
                         .DeriveWithWeight(weight));
  label->SetProperty(
      views::kFlexBehaviorKey,
      views::FlexSpecification(views::LayoutOrientation::kHorizontal,
                               views::MinimumFlexSizeRule::kScaleToZero,
                               views::MaximumFlexSizeRule::kUnbounded));
  return label;
}

}  // namespace

ActivityCard::ActivityCard(base::RepeatingClosure undo,
                           base::RepeatingClosure accept)
    : undo_(std::move(undo)), accept_(std::move(accept)) {
  SetOrientation(views::LayoutOrientation::kVertical);
  SetInteriorMargin(gfx::Insets(kCardPadding));
  SetBackground(views::CreateRoundedRectBackground(ui::kColorSysHeaderContainer,
                                                   kCardRadius));

  auto* header = AddChildView(Row());
  header->SetProperty(views::kMarginsKey, gfx::Insets());
  throbber_ =
      header->AddChildView(std::make_unique<views::Throbber>(kIconSize));
  throbber_->SetColorId(ui::kColorSysOnHeaderPrimary);
  icon_ = header->AddChildView(std::make_unique<views::ImageView>());
  headline_ =
      header->AddChildView(Line(/*size_delta=*/0, gfx::Font::Weight::SEMIBOLD));
  headline_->SetProperty(views::kMarginsKey,
                         gfx::Insets::TLBR(0, kIconGap, 0, 0));

  page_ = AddChildView(Line(kSmallerText, gfx::Font::Weight::NORMAL));
  page_->SetProperty(views::kMarginsKey,
                     gfx::Insets::TLBR(kLineSpacing, 0, 0, 0));
  source_ = AddChildView(Line(kSmallerText, gfx::Font::Weight::NORMAL));
  source_->SetProperty(views::kMarginsKey,
                       gfx::Insets::TLBR(kLineSpacing, 0, 0, 0));

  meter_ = AddChildView(std::make_unique<views::ProgressBar>());
  meter_->SetPreferredHeight(kMeterHeight);
  meter_->SetForegroundColorId(ui::kColorSysOnHeaderPrimary);
  meter_->SetBackgroundColorId(ui::kColorSysOnHeaderDivider);
  meter_->SetProperty(views::kMarginsKey,
                      gfx::Insets::TLBR(kMeterSpacing, 0, 0, 0));

  confidence_ = AddChildView(Line(kSmallerText, gfx::Font::Weight::NORMAL));
  confidence_->SetProperty(views::kMarginsKey,
                           gfx::Insets::TLBR(kLineSpacing, 0, 0, 0));

  // The button has a row to itself, so no line of text gives way to it.
  action_ =
      AddChildView(std::make_unique<views::MdTextButton>(base::BindRepeating(
          &ActivityCard::OnActionPressed, base::Unretained(this))));
  // The card already has the pill's fill, so the button takes the active
  // tab's.
  action_->SetBgColorIdOverride(kColorTabBackgroundActiveFrameActive);
  action_->SetStrokeColorIdOverride(kColorTabBackgroundActiveFrameActive);
  action_->SetEnabledTextColors(kColorTabForegroundActiveFrameActive);
  action_->SetCustomPadding(kActionPadding);
  action_->SetMinSize(gfx::Size());
  action_->SetProperty(views::kCrossAxisAlignmentKey,
                       views::LayoutAlignment::kEnd);
  action_->SetProperty(views::kMarginsKey,
                       gfx::Insets::TLBR(kActionSpacing, 0, 0, 0));
}

ActivityCard::~ActivityCard() = default;

void ActivityCard::Show(const Event& event) {
  const bool waits = Waits(event.kind);
  const bool has_confidence = HasConfidence(event.kind);
  throbber_->SetVisible(waits);
  if (waits) {
    throbber_->Start();
  } else {
    throbber_->Stop();
  }
  icon_->SetVisible(!waits);
  icon_->SetImage(ui::ImageModel::FromVectorIcon(
      IconFor(event.kind), ui::kColorSysOnHeaderPrimary, kIconSize));
  headline_->SetText(Headline(event));
  page_->SetText(event.page);

  // A request that failed may never have been answered.
  source_->SetVisible(!waits && event.kind != Kind::kRequestFailed);
  source_->SetText(l10n_util::GetStringFUTF16(
      IDS_UW_ACTIVITY_SOURCE, event.model, Number(event.took.InSecondsF(), 1)));

  const std::u16string value = Number(event.confidence, 2);
  const std::u16string needed = Number(event.needed, 2);
  const std::u16string hint =
      has_confidence ? l10n_util::GetStringFUTF16(
                           IDS_UW_ACTIVITY_CONFIDENCE_HINT, value, needed)
                     : std::u16string(headline_->GetText());
  meter_->SetVisible(waits || has_confidence);
  meter_->SetValue(waits ? kStillWaiting : event.confidence);
  meter_->GetViewAccessibility().SetName(hint);
  confidence_->SetVisible(has_confidence);
  confidence_->SetText(
      l10n_util::GetStringFUTF16(IDS_UW_ACTIVITY_CONFIDENCE, value, needed));
  confidence_->SetCustomTooltipText(hint);

  accepts_ = event.kind == Kind::kUnsure;
  action_->SetVisible(accepts_ || OffersUndo(event.kind));
  action_->SetText(
      l10n_util::GetStringUTF16(accepts_ ? IDS_CONFIRM : IDS_UW_AUTO_UNDO));
  SetVisible(true);
}

void ActivityCard::Hide() {
  throbber_->Stop();
  if (waiting()) {
    meter_->SetValue(0.0);
  }
  SetVisible(false);
}

std::u16string_view ActivityCard::headline() const {
  return headline_->GetText();
}

std::u16string_view ActivityCard::page() const {
  return page_->GetText();
}

std::u16string_view ActivityCard::confidence() const {
  return confidence_->GetVisible() ? confidence_->GetText()
                                   : std::u16string_view();
}

std::u16string_view ActivityCard::source() const {
  return source_->GetVisible() ? source_->GetText() : std::u16string_view();
}

bool ActivityCard::waiting() const {
  return throbber_->GetVisible();
}

bool ActivityCard::offers_undo() const {
  return action_->GetVisible() && !accepts_;
}

bool ActivityCard::offers_accept() const {
  return action_->GetVisible() && accepts_;
}

void ActivityCard::OnActionPressed() {
  (accepts_ ? accept_ : undo_).Run();
}

BEGIN_METADATA(ActivityCard)
END_METADATA

}  // namespace uw
