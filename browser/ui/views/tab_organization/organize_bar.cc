// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/browser/ui/views/tab_organization/organize_bar.h"

#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "base/strings/stringprintf.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/sequenced_task_runner.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/color/chrome_color_id.h"
#include "chrome/browser/ui/layout_constants.h"
#include "chrome/browser/ui/tabs/vertical_tab_strip_state_controller.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/tab_strip_region_view.h"
#include "chrome/grit/generated_resources.h"
#include "components/prefs/pref_service.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/mojom/menu_source_type.mojom.h"
#include "ui/views/bubble/bubble_dialog_delegate_view.h"
#include "ui/views/controls/button/md_text_button.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/menu/menu_runner.h"
#include "ui/views/layout/flex_layout.h"
#include "ui/views/view_class_properties.h"
#include "uw/browser/ai_providers/models.h"
#include "uw/browser/tab_organization/ai_connection.h"
#include "uw/browser/tab_organization/tab_organization_controller.h"
#include "uw/browser/ui/views/tab_organization/activity_card.h"
#include "uw/browser/ui/views/tab_organization/new_button.h"
#include "uw/browser/ui/views/tab_organization/organize_card.h"
#include "uw/browser/ui/views/tab_organization/tint_style.h"
#include "uw/browser/ui/views/tab_organization/window_commands.h"
#include "uw/browser/ui/views/theme_picker/theme_picker.h"

namespace uw {
namespace {

using Kind = AutomaticOrganizer::Event::Kind;

// How long the card stays after a move, and after any other answer.
constexpr base::TimeDelta kChangeDuration = base::Seconds(6);
constexpr base::TimeDelta kAnswerDuration = base::Seconds(8);
// A suggestion stays longer, to leave time to confirm it.
constexpr base::TimeDelta kSuggestionDuration = base::Seconds(15);
// Longer than a request can take, so the card cannot outlive its request.
constexpr base::TimeDelta kAskingDuration = kProviderTimeout + base::Seconds(5);
constexpr int kRowSpacing = 4;

}  // namespace

OrganizeBar::OrganizeBar(BrowserWindowInterface* window)
    : window_(window), theme_picker_(std::make_unique<ThemePicker>(window)) {
  SetOrientation(views::LayoutOrientation::kVertical);
  SetProperty(
      views::kMarginsKey,
      gfx::Insets::VH(kRowSpacing,
                      GetLayoutConstant(
                          LayoutConstant::kVerticalTabStripHorizontalPadding)));

  card_ = AddChildView(std::make_unique<ActivityCard>(
      base::BindRepeating(&OrganizeBar::Undo, base::Unretained(this)),
      base::BindRepeating(&OrganizeBar::AcceptSuggestion,
                          base::Unretained(this)),
      base::BindRepeating(&OrganizeBar::AskForNameAgain,
                          base::Unretained(this))));
  card_->Hide();

  // Organize at the left, the new button at the right end.
  row_ = AddChildView(std::make_unique<views::FlexLayoutView>());
  row_->SetOrientation(views::LayoutOrientation::kHorizontal);
  row_->SetCrossAxisAlignment(views::LayoutAlignment::kCenter);
  row_->SetProperty(views::kMarginsKey,
                    gfx::Insets::TLBR(kRowSpacing, 0, 0, 0));
  button_ = row_->AddChildView(std::make_unique<views::MdTextButton>(
      base::BindRepeating(&OrganizeBar::ShowMenu, base::Unretained(this)),
      l10n_util::GetStringUTF16(IDS_UW_ORGANIZE)));
  StyleAsPill(button_);
  spacer_ = row_->AddChildView(std::make_unique<views::View>());
  spacer_->SetProperty(
      views::kFlexBehaviorKey,
      views::FlexSpecification(views::LayoutOrientation::kHorizontal,
                               views::MinimumFlexSizeRule::kScaleToZero,
                               views::MaximumFlexSizeRule::kUnbounded));
  new_button_ = row_->AddChildView(std::make_unique<NewButton>(window_));

  if (AutomaticOrganizer* organizer = AutomaticOrganizer::From(window_)) {
    event_subscription_ = organizer->ObserveEvents(base::BindRepeating(
        &OrganizeBar::OnOrganizerEvent, base::Unretained(this)));
  }
  if (TabOrganizationController* controller =
          TabOrganizationController::From(window_)) {
    manual_move_subscription_ =
        controller->ObserveManualMoves(base::BindRepeating(
            [](OrganizeBar* bar, const std::vector<PageId>&) {
              if (bar->card_->offers_undo() || bar->card_->offers_accept()) {
                bar->HideCard();
              }
            },
            base::Unretained(this)));
  }
  tabs::VerticalTabStripStateController* state =
      tabs::VerticalTabStripStateController::From(window_);
  collapse_subscription_ = state->RegisterOnCollapseChanged(base::BindRepeating(
      &OrganizeBar::OnCollapseChanged, base::Unretained(this)));
  OnCollapseChanged(state->GetCollapseState());
}

OrganizeBar::~OrganizeBar() {
  card_widget_.reset();
}

bool OrganizeBar::card_visible() const {
  return card_->GetVisible();
}

void OrganizeBar::Undo() {
  if (TabOrganizationController* controller =
          TabOrganizationController::From(window_)) {
    controller->Undo();
  }
  HideCard();
}

void OrganizeBar::AskForNameAgain(const std::string& task) {
  HideCard();
  if (AutomaticOrganizer* organizer = AutomaticOrganizer::From(window_)) {
    organizer->AskForName(task);
  }
}

void OrganizeBar::AcceptSuggestion() {
  if (AutomaticOrganizer* organizer = AutomaticOrganizer::From(window_)) {
    organizer->AcceptSuggestion();
  }
}

bool OrganizeBar::IsCommandIdChecked(int command_id) const {
  return IsWindowCommandChecked(window_, command_id);
}

bool OrganizeBar::IsCommandIdEnabled(int command_id) const {
  return CanRunWindowCommand(window_, command_id);
}

void OrganizeBar::ExecuteCommand(int command_id, int event_flags) {
  if (command_id == kUndo) {
    Undo();
    return;
  }
  RunWindowCommand(window_, command_id);
}

bool OrganizeBar::GetAcceleratorForCommandId(
    int command_id,
    ui::Accelerator* accelerator) const {
  return GetCommandAccelerator(window_, command_id, accelerator);
}

void OrganizeBar::OnCollapseChanged(tabs::VerticalTabStripCollapseState state) {
  collapsed_ = state != tabs::VerticalTabStripCollapseState::kExpanded;
  // The narrow sidebar keeps only the new button's "+", centered.
  button_->SetVisible(!collapsed_);
  spacer_->SetVisible(!collapsed_);
  row_->SetMainAxisAlignment(collapsed_ ? views::LayoutAlignment::kCenter
                                        : views::LayoutAlignment::kStart);
  new_button_->SetCompact(collapsed_);
  if (collapsed_) {
    HideCard();
  }
}

void OrganizeBar::ShowMenu() {
  if (card_widget_) {
    return;
  }
  card_bubble_ = OrganizeCard::CreateBubble(button_, this, window_);
  card_widget_ = views::BubbleDialogDelegate::CreateBubble(
      card_bubble_.get(),
      base::BindOnce(&OrganizeBar::OnCardClosed, base::Unretained(this)));
  hover_lock_ =
      BrowserView::GetBrowserViewForBrowser(window_)
          ->tab_strip_view()
          ->GetExpandOnHoverLock(ExpandOnHoverLockType::kKeepCurrentState);
  card_widget_->Show();
}

// The card closes from inside its own widget, so it is deleted afterwards,
// the widget before the bubble it uses.
void OrganizeBar::OnCardClosed(views::Widget::ClosedReason reason) {
  hover_lock_.reset();
  base::SequencedTaskRunner::GetCurrentDefault()->DeleteSoon(
      FROM_HERE, std::move(card_widget_));
  base::SequencedTaskRunner::GetCurrentDefault()->DeleteSoon(
      FROM_HERE, std::move(card_bubble_));
}

void OrganizeBar::ShowContextMenuForViewImpl(
    views::View* source,
    const gfx::Point& point,
    ui::mojom::MenuSourceType source_type) {
  menu_model_ = std::make_unique<ui::SimpleMenuModel>(this);
  menu_model_->AddItemWithStringId(kTheme, IDS_UW_THEME_MENU);
  hover_lock_ =
      BrowserView::GetBrowserViewForBrowser(window_)
          ->tab_strip_view()
          ->GetExpandOnHoverLock(ExpandOnHoverLockType::kKeepCurrentState);
  menu_runner_ = std::make_unique<views::MenuRunner>(
      menu_model_.get(), views::MenuRunner::CONTEXT_MENU,
      base::BindRepeating(&OrganizeBar::OnMenuClosed, base::Unretained(this)));
  menu_runner_->RunMenuAt(GetWidget(), nullptr, gfx::Rect(point, gfx::Size()),
                          views::MenuAnchorPosition::kTopLeft, source_type);
}

void OrganizeBar::OnMenuClosed() {
  hover_lock_.reset();
}

bool OrganizeBar::ShowsActivityDetails() const {
  return window_->GetProfile()->GetPrefs()->GetBoolean(
      kOrganizationShowActivityPref);
}

// A change that can be undone always shows, and so do a failed Name Task,
// which the user asked for, and the first failed request after one that
// worked. The rest, and the model's numbers, show only with Show Activity on.
void OrganizeBar::OnOrganizerEvent(const AutomaticOrganizer::Event& event) {
  const bool asking =
      event.kind == Kind::kAsking || event.kind == Kind::kNaming;
  const bool changed =
      event.kind == Kind::kMoved || event.kind == Kind::kCreated;
  const bool failed = event.kind == Kind::kRequestFailed;
  const bool first_failure = failed && !failing_;
  const bool name_failed = event.kind == Kind::kNameFailed;
  if (!asking && !name_failed) {
    failing_ = failed;
  }
  const bool details = ShowsActivityDetails();
  if (collapsed_ || (!details && !changed && !first_failure && !name_failed)) {
    return;
  }
  card_->Show(event, details);
  card_timer_.Start(
      FROM_HERE,
      asking                                       ? kAskingDuration
      : changed                                    ? kChangeDuration
      : event.kind == Kind::kUnsure || name_failed ? kSuggestionDuration
                                                   : kAnswerDuration,
      base::BindOnce(&OrganizeBar::HideCard, base::Unretained(this)));
}

void OrganizeBar::HideCard() {
  card_timer_.Stop();
  card_->Hide();
}

BEGIN_METADATA(OrganizeBar)
END_METADATA

}  // namespace uw
