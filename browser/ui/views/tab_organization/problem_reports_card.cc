// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/browser/ui/views/tab_organization/problem_reports_card.h"

#include <memory>
#include <utility>

#include "base/functional/bind.h"
#include "chrome/browser/ui/color/chrome_color_id.h"
#include "chrome/grit/generated_resources.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/models/image_model.h"
#include "ui/color/color_id.h"
#include "ui/color/color_provider.h"
#include "ui/gfx/font.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/size.h"
#include "ui/views/controls/button/md_text_button.h"
#include "ui/views/controls/image_view.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/link.h"
#include "ui/views/layout/flex_layout.h"
#include "ui/views/view_class_properties.h"
#include "uw/browser/problem_reports/problem_reports.h"
#include "uw/browser/ui/vector_icons/vector_icons.h"
#include "uw/browser/ui/views/tab_organization/tint_style.h"

namespace uw {
namespace {

// As the activity card above it.
constexpr int kCardRadius = 10;
constexpr int kCardPadding = 10;
constexpr int kIconSize = 14;
constexpr int kIconGap = 6;
constexpr int kParagraphSpacing = 6;
constexpr int kButtonSpacing = 8;
constexpr int kButtonGap = 6;
constexpr gfx::Insets kButtonPadding = gfx::Insets::VH(3, 10);
constexpr int kSmallerText = -1;

std::unique_ptr<views::Label> Paragraph(int message) {
  auto label =
      std::make_unique<views::Label>(l10n_util::GetStringUTF16(message));
  StyleOnTint(label.get());
  label->SetMultiLine(true);
  label->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  label->SetFontList(label->font_list().DeriveWithSizeDelta(kSmallerText));
  label->SetProperty(views::kMarginsKey,
                     gfx::Insets::TLBR(kParagraphSpacing, 0, 0, 0));
  return label;
}

// The two answers look the same, so neither is the obvious one.
std::unique_ptr<views::MdTextButton> Answer(int message, bool send) {
  auto button = std::make_unique<views::MdTextButton>(
      base::BindRepeating(
          [](bool send) { ProblemReports::Get().Answer(send); }, send),
      l10n_util::GetStringUTF16(message));
  button->SetBgColorIdOverride(kColorTabBackgroundActiveFrameActive);
  button->SetStrokeColorIdOverride(kColorTabBackgroundActiveFrameActive);
  button->SetEnabledTextColors(kColorTabForegroundActiveFrameActive);
  button->SetCustomPadding(kButtonPadding);
  button->SetMinSize(gfx::Size());
  button->SetHorizontalAlignment(gfx::ALIGN_CENTER);
  button->SetProperty(
      views::kFlexBehaviorKey,
      views::FlexSpecification(views::LayoutOrientation::kHorizontal,
                               views::MinimumFlexSizeRule::kScaleToMinimum,
                               views::MaximumFlexSizeRule::kUnbounded)
          .WithWeight(1));
  return button;
}

}  // namespace

ProblemReportsCard::ProblemReportsCard(GlassOf glass,
                                       base::RepeatingClosure open_settings) {
  SetOrientation(views::LayoutOrientation::kVertical);
  SetInteriorMargin(gfx::Insets(kCardPadding));
  SetBackground(CreateGlassCardBackground(
      std::move(glass), ui::kColorSysHeaderContainer, kCardRadius));

  auto* header = AddChildView(std::make_unique<views::FlexLayoutView>());
  header->SetCrossAxisAlignment(views::LayoutAlignment::kCenter);
  header->AddChildView(
      std::make_unique<views::ImageView>(ui::ImageModel::FromVectorIcon(
          kLifeBuoyIcon, ui::kColorSysOnHeaderPrimary, kIconSize)));
  auto* title = header->AddChildView(std::make_unique<views::Label>(
      l10n_util::GetStringUTF16(IDS_UW_PROBLEM_REPORTS_TITLE)));
  StyleOnTint(title);
  title->SetFontList(
      title->font_list().DeriveWithWeight(gfx::Font::Weight::SEMIBOLD));
  title->SetProperty(views::kMarginsKey, gfx::Insets::TLBR(0, kIconGap, 0, 0));

  AddChildView(Paragraph(IDS_UW_PROBLEM_REPORTS_WHAT));
  AddChildView(Paragraph(IDS_UW_PROBLEM_REPORTS_PRIVACY));

  auto* answers = AddChildView(std::make_unique<views::FlexLayoutView>());
  answers->SetDefault(views::kMarginsKey,
                      gfx::Insets::TLBR(0, 0, 0, kButtonGap));
  answers->SetCollapseMargins(true);
  answers->SetIgnoreDefaultMainAxisMargins(true);
  answers->SetProperty(views::kMarginsKey,
                       gfx::Insets::TLBR(kButtonSpacing, 0, 0, 0));
  send_ = answers->AddChildView(
      Answer(IDS_UW_PROBLEM_REPORTS_SEND, /*send=*/true));
  dont_send_ = answers->AddChildView(
      Answer(IDS_UW_PROBLEM_REPORTS_DONT_SEND, /*send=*/false));

  whats_sent_ = AddChildView(std::make_unique<views::Link>(
      l10n_util::GetStringUTF16(IDS_UW_PROBLEM_REPORTS_WHATS_SENT)));
  whats_sent_->SetCallback(std::move(open_settings));
  whats_sent_->SetForceUnderline(true);
  whats_sent_->SetFontList(
      whats_sent_->font_list().DeriveWithSizeDelta(kSmallerText));
  whats_sent_->SetProperty(views::kCrossAxisAlignmentKey,
                           views::LayoutAlignment::kStart);
  whats_sent_->SetProperty(views::kMarginsKey,
                           gfx::Insets::TLBR(kParagraphSpacing, 0, 0, 0));

  answer_subscription_ = ProblemReports::Get().ObserveAnswer(
      base::BindRepeating(&ProblemReportsCard::Update, base::Unretained(this)));
  Update();
}

ProblemReportsCard::~ProblemReportsCard() = default;

void ProblemReportsCard::OnThemeChanged() {
  views::FlexLayoutView::OnThemeChanged();
  // A link takes a fixed color only: the tint's text color, as the paragraphs
  // above it have.
  whats_sent_->SetEnabledColor(
      GetColorProvider()->GetColor(kColorTabForegroundInactiveFrameActive));
}

void ProblemReportsCard::Update() {
  SetVisible(ProblemReports::Get().ShouldAsk());
}

BEGIN_METADATA(ProblemReportsCard)
END_METADATA

}  // namespace uw
