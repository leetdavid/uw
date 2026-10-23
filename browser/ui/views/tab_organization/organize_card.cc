// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/browser/ui/views/tab_organization/organize_card.h"

#include <algorithm>
#include <memory>
#include <optional>
#include <utility>

#include "base/functional/bind.h"
#include "base/numerics/safe_conversions.h"
#include "base/strings/strcat.h"
#include "base/task/sequenced_task_runner.h"
#include "base/time/time.h"
#include "cc/paint/paint_flags.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/grit/generated_resources.h"
#include "components/prefs/pref_service.h"
#include "components/vector_icons/vector_icons.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/base/accelerators/accelerator.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/color/color_provider.h"
#include "ui/gfx/canvas.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/bubble/bubble_dialog_delegate_view.h"
#include "ui/views/controls/button/toggle_button.h"
#include "ui/views/controls/label.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/layout/flex_layout_view.h"
#include "ui/views/view_class_properties.h"
#include "ui/views/widget/widget.h"
#include "uw/browser/tab_organization/ai_connection.h"
#include "uw/browser/tab_organization/commands.h"
#include "uw/browser/tab_organization/tab_organization_controller.h"
#include "uw/browser/ui/look/menu_card.h"
#include "uw/browser/ui/views/tab_organization/menu_card_row.h"
#include "uw/browser/ui/views/tab_organization/organize_bar.h"
#include "uw/browser/ui/views/tab_organization/window_commands.h"

namespace uw {
namespace {

constexpr int kDotSize = 8;
constexpr int kDotMargin = 4;
constexpr int kStatusHeight = 34;
constexpr int kStatusGap = 10;
constexpr int kSideInset = 10;
constexpr int kChipGap = 4;
constexpr int kMinutesInAnHour = 60;
constexpr float kRingWidth = 1.5f;
constexpr SkColor kOrganizingColor = SkColorSetRGB(0x18, 0x80, 0x38);
constexpr SkColor kPausedColor = SkColorSetRGB(0xE8, 0x71, 0x0A);
// The countdown is shown in minutes, so this keeps it within a few seconds.
constexpr base::TimeDelta kCountdownStep = base::Seconds(5);

enum class State { kOrganizing, kPaused, kOff, kNeedsModel };

State StateOf(const PrefService* prefs) {
  if (!prefs->GetBoolean(kOrganizationEnabledPref)) {
    return State::kOff;
  }
  if (!OrganizationReady(prefs)) {
    return State::kNeedsModel;
  }
  return ReadPauseState(prefs) == PauseMode::kOff ? State::kOrganizing
                                                  : State::kPaused;
}

std::u16string StatusOf(const PrefService* prefs) {
  switch (StateOf(prefs)) {
    case State::kOrganizing:
      return l10n_util::GetStringUTF16(IDS_UW_ORGANIZE_ORGANIZING);
    case State::kOff:
      return l10n_util::GetStringUTF16(IDS_UW_ORGANIZE_OFF);
    case State::kNeedsModel:
      return l10n_util::GetStringUTF16(IDS_UW_ORGANIZE_NEEDS_MODEL);
    case State::kPaused:
      break;
  }
  switch (ReadPauseState(prefs)) {
    case PauseMode::kTimed: {
      const int minutes = std::max(
          1, base::ClampCeil(
                 (PausedUntil(prefs) - base::Time::Now()).InSecondsF() /
                 base::Time::kSecondsPerMinute));
      return minutes >= kMinutesInAnHour
                 ? l10n_util::GetStringUTF16(IDS_UW_ORGANIZE_PAUSED_HOUR)
                 : l10n_util::GetPluralStringFUTF16(
                       IDS_UW_ORGANIZE_PAUSED_MINUTES, minutes);
    }
    case PauseMode::kUntilLaunch:
      return l10n_util::GetStringUTF16(IDS_UW_ORGANIZE_PAUSED_UNTIL_RESTART);
    default:
      return l10n_util::GetStringUTF16(IDS_UW_ORGANIZE_PAUSED);
  }
}

// An 8px dot: green while organizing, amber while paused, a gray ring when
// off or waiting for a model.
class StatusDot : public views::View {
  METADATA_HEADER(StatusDot, views::View)

 public:
  explicit StatusDot(std::optional<SkColor> fill) : fill_(fill) {
    SetPreferredSize(gfx::Size(kDotSize, kDotSize));
    SetProperty(views::kMarginsKey, gfx::Insets::VH(0, kDotMargin));
  }

  void OnPaint(gfx::Canvas* canvas) override {
    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    const gfx::PointF center = gfx::RectF(GetLocalBounds()).CenterPoint();
    if (fill_) {
      flags.setColor(*fill_);
      canvas->DrawCircle(center, kDotSize / 2.0f, flags);
      return;
    }
    flags.setStyle(cc::PaintFlags::kStroke_Style);
    flags.setStrokeWidth(kRingWidth);
    flags.setColor(GetColorProvider()->GetColor(kColorMenuCardMuted));
    canvas->DrawCircle(center, (kDotSize - kRingWidth) / 2.0f, flags);
  }

 private:
  const std::optional<SkColor> fill_;
};

BEGIN_METADATA(StatusDot)
END_METADATA

}  // namespace

// static
std::unique_ptr<views::BubbleDialogDelegate> OrganizeCard::CreateBubble(
    views::View* anchor,
    OrganizeBar* bar,
    BrowserWindowInterface* window) {
  auto bubble = std::make_unique<views::BubbleDialogDelegate>(
      anchor, views::BubbleBorder::BOTTOM_LEFT);
  SetUpMenuCard(bubble.get());
  bubble->set_highlight_button_when_shown(false);
  bubble->SetContentsView(std::make_unique<OrganizeCard>(bar, window));
  return bubble;
}

OrganizeCard::OrganizeCard(OrganizeBar* bar, BrowserWindowInterface* window)
    : bar_(bar), window_(window) {
  LayOutMenuCard(this);
  prefs_.Init(window->GetProfile()->GetPrefs());
  for (const char* pref :
       {kOrganizationEnabledPref, kOrganizationPauseModePref,
        kOrganizationPauseUntilPref, kOrganizationShowActivityPref}) {
    prefs_.Add(pref, base::BindRepeating(&OrganizeCard::ScheduleRebuild,
                                         base::Unretained(this)));
  }
  if (TabOrganizationController* controller =
          TabOrganizationController::From(window)) {
    tree_subscription_ = controller->Observe(base::BindRepeating(
        &OrganizeCard::ScheduleRebuild, base::Unretained(this)));
  }
  countdown_.Start(
      FROM_HERE, kCountdownStep,
      base::BindRepeating(&OrganizeCard::UpdateStatus, base::Unretained(this)));
  Rebuild();
}

OrganizeCard::~OrganizeCard() = default;

std::u16string OrganizeCard::StatusForTesting() const {
  return base::StrCat({status_bold_->GetText(), status_rest_->GetText()});
}

void OrganizeCard::ScheduleRebuild() {
  if (rebuild_pending_) {
    return;
  }
  rebuild_pending_ = true;
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE,
      base::BindOnce(&OrganizeCard::Rebuild, weak_factory_.GetWeakPtr()));
}

void OrganizeCard::Rebuild() {
  rebuild_pending_ = false;
  status_bold_ = nullptr;
  status_rest_ = nullptr;
  RemoveAllChildViews();
  AddStatus();
  AddAction();
  AddChildView(MakeMenuCardSeparator());
  AddUndo();
  AddActivity();
  AddChildView(MakeMenuCardSeparator());
  auto settings = std::make_unique<MenuCardRow>(
      base::BindRepeating(&OrganizeCard::RunAndClose, base::Unretained(this),
                          kShowOrganizationSettings),
      vector_icons::kSettingsIcon,
      l10n_util::GetStringUTF16(IDS_UW_ORGANIZE_SETTINGS));
  settings->SetID(kSettingsId);
  AddChildView(std::move(settings));
  if (views::Widget* widget = GetWidget()) {
    widget->widget_delegate()->AsBubbleDialogDelegate()->SizeToContents();
  }
}

void OrganizeCard::AddStatus() {
  const State state = StateOf(prefs_.prefs());
  auto status = std::make_unique<views::View>();
  status->SetID(kStatusId);
  auto* layout = status->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal,
      gfx::Insets::VH(0, kSideInset), kStatusGap - kDotMargin));
  layout->set_minimum_cross_axis_size(kStatusHeight);
  layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);
  status->AddChildView(std::make_unique<StatusDot>(
      state == State::kOrganizing ? std::optional(kOrganizingColor)
      : state == State::kPaused   ? std::optional(kPausedColor)
                                  : std::nullopt));
  views::View* words = status->AddChildView(std::make_unique<views::View>());
  words->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal));
  status_bold_ = words->AddChildView(std::make_unique<views::Label>());
  status_bold_->SetFontList(
      status_bold_->font_list().DeriveWithWeight(gfx::Font::Weight::SEMIBOLD));
  status_rest_ = words->AddChildView(std::make_unique<views::Label>());
  for (views::Label* label : {status_bold_.get(), status_rest_.get()}) {
    label->SetEnabledColor(kColorMenuCardText);
  }
  AddChildView(std::move(status));
  UpdateStatus();
}

// The first word of the status is in bold: "Paused", then the rest.
void OrganizeCard::UpdateStatus() {
  const std::u16string status = StatusOf(prefs_.prefs());
  const std::u16string paused =
      l10n_util::GetStringUTF16(IDS_UW_ORGANIZE_PAUSED);
  const size_t bold =
      status.starts_with(paused) ? paused.size() : status.size();
  status_bold_->SetText(status.substr(0, bold));
  status_rest_->SetText(status.substr(bold));
}

void OrganizeCard::AddAction() {
  switch (StateOf(prefs_.prefs())) {
    case State::kOrganizing: {
      AddChildView(
          MakeMenuCardHeader(vector_icons::kPauseIcon,
                             l10n_util::GetStringUTF16(IDS_UW_ORGANIZE_PAUSE)));
      auto chips = std::make_unique<views::View>();
      chips->SetLayoutManager(std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kHorizontal,
          gfx::Insets::TLBR(2, kSideInset, kMenuCardPadding, kChipGap),
          kChipGap));
      const struct {
        int command;
        int text;
        Id id;
      } lengths[] = {
          {kPauseOrganizationForAnHour, IDS_UW_ORGANIZE_PAUSE_HOUR,
           kPauseHourId},
          {kPauseOrganizationUntilLaunch, IDS_UW_ORGANIZE_PAUSE_UNTIL_RESTART,
           kPauseUntilRestartId},
          {kPauseOrganizationIndefinitely, IDS_UW_ORGANIZE_PAUSE_UNTIL_RESUMED,
           kPauseUntilResumedId},
      };
      for (const auto& length : lengths) {
        auto chip = std::make_unique<MenuCardChip>(
            base::BindRepeating(&OrganizeCard::Run, base::Unretained(this),
                                length.command),
            l10n_util::GetStringUTF16(length.text));
        chip->SetID(length.id);
        chips->AddChildView(std::move(chip));
      }
      AddChildView(std::move(chips));
      return;
    }
    case State::kPaused: {
      auto resume = std::make_unique<MenuCardRow>(
          base::BindRepeating(&OrganizeCard::Run, base::Unretained(this),
                              kResumeOrganization),
          vector_icons::kPlayArrowIcon,
          l10n_util::GetStringUTF16(IDS_UW_ORGANIZE_RESUME));
      resume->SetID(kResumeId);
      AddChildView(std::move(resume));
      return;
    }
    case State::kOff: {
      auto turn_on = std::make_unique<MenuCardRow>(
          base::BindRepeating(
              [](PrefService* prefs) {
                prefs->SetBoolean(kOrganizationEnabledPref, true);
              },
              prefs_.prefs()),
          vector_icons::kPlayArrowIcon,
          l10n_util::GetStringUTF16(IDS_UW_ORGANIZE_TURN_ON));
      turn_on->SetID(kTurnOnId);
      AddChildView(std::move(turn_on));
      return;
    }
    case State::kNeedsModel: {
      auto set_up = std::make_unique<MenuCardRow>(
          base::BindRepeating(&OrganizeCard::OpenProviders,
                              base::Unretained(this)),
          vector_icons::kLinkIcon,
          l10n_util::GetStringUTF16(IDS_UW_ORGANIZE_SET_UP));
      set_up->SetID(kSetUpId);
      AddChildView(std::move(set_up));
      return;
    }
  }
}

void OrganizeCard::AddUndo() {
  TabOrganizationController* controller =
      TabOrganizationController::From(window_);
  const std::optional<TabOrganizationController::UndoMove> move =
      controller ? controller->NextUndo() : std::nullopt;
  if (!move) {
    auto nothing = std::make_unique<MenuCardRow>(
        views::Button::PressedCallback(), vector_icons::kUndoIcon,
        l10n_util::GetStringUTF16(IDS_UW_ORGANIZE_NOTHING_TO_UNDO));
    nothing->SetEnabled(false);
    nothing->SetID(kUndoId);
    AddChildView(std::move(nothing));
    return;
  }
  auto undo = std::make_unique<MenuCardRow>(
      base::BindRepeating(&OrganizeCard::RunAndClose, base::Unretained(this),
                          static_cast<int>(OrganizeBar::kUndo)),
      vector_icons::kUndoIcon,
      l10n_util::GetStringUTF16(IDS_UW_ORGANIZE_UNDO_MOVE));
  undo->SetID(kUndoId);
  ui::Accelerator key;
  if (bar_->GetAcceleratorForCommandId(OrganizeBar::kUndo, &key)) {
    undo->SetEnd(MakeMenuCardKey(key));
  }
  // The tab's title gives way before the task's name does.
  auto detail = std::make_unique<views::FlexLayoutView>();
  detail->SetDefault(views::kMarginsKey, gfx::Insets::TLBR(0, 0, 0, kChipGap));
  views::Label* tab = detail->AddChildView(MakeMenuCardDetail(move->tab));
  tab->SetProperty(
      views::kFlexBehaviorKey,
      views::FlexSpecification(views::MinimumFlexSizeRule::kScaleToZero,
                               views::MaximumFlexSizeRule::kPreferred));
  if (!move->place.empty()) {
    detail->AddChildView(MakeMenuCardDetail(
        l10n_util::GetStringFUTF16(IDS_UW_ORGANIZE_UNDO_PLACE, move->place)));
  }
  undo->GetViewAccessibility().SetDescription(
      move->place.empty() ? move->tab
                          : move->tab + u" " +
                                l10n_util::GetStringFUTF16(
                                    IDS_UW_ORGANIZE_UNDO_PLACE, move->place));
  undo->SetDetail(std::move(detail));
  AddChildView(std::move(undo));
}

void OrganizeCard::AddActivity() {
  const bool on = prefs_.prefs()->GetBoolean(kOrganizationShowActivityPref);
  auto activity = std::make_unique<MenuCardRow>(
      base::BindRepeating(&OrganizeCard::Run, base::Unretained(this),
                          kShowOrganizerActivity),
      vector_icons::kVisibilityIcon,
      l10n_util::GetStringUTF16(IDS_UW_ORGANIZE_SHOW_ACTIVITY));
  activity->SetID(kActivityId);
  activity->GetViewAccessibility().SetRole(ax::mojom::Role::kSwitch);
  activity->GetViewAccessibility().SetCheckedState(
      on ? ax::mojom::CheckedState::kTrue : ax::mojom::CheckedState::kFalse);
  auto toggle = std::make_unique<views::ToggleButton>();
  toggle->SetIsOn(on);
  toggle->SetTrackOnColor(kColorMenuCardAccent);
  toggle->SetCanProcessEventsWithinSubtree(false);
  toggle->SetFocusBehavior(FocusBehavior::NEVER);
  toggle->GetViewAccessibility().SetIsIgnored(true);
  activity->SetEnd(std::move(toggle));
  AddChildView(std::move(activity));
}

void OrganizeCard::Run(int command) {
  bar_->ExecuteCommand(command, 0);
}

void OrganizeCard::RunAndClose(int command) {
  bar_->ExecuteCommand(command, 0);
  GetWidget()->Close();
}

void OrganizeCard::OpenProviders() {
  ShowAiProviders(window_);
  GetWidget()->Close();
}

BEGIN_METADATA(OrganizeCard)
END_METADATA

}  // namespace uw
