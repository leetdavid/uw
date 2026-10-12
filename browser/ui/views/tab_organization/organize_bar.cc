// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/browser/ui/views/tab_organization/organize_bar.h"

#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "base/strings/stringprintf.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/chrome_pages.h"
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
#include "ui/views/controls/button/md_text_button.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/menu/menu_runner.h"
#include "ui/views/layout/flex_layout.h"
#include "ui/views/view_class_properties.h"
#include "uw/browser/tab_organization/ai_connection.h"
#include "uw/browser/tab_organization/tab_organization_controller.h"
#include "uw/browser/ui/views/tab_organization/activity_card.h"
#include "uw/browser/ui/views/tab_organization/tint_style.h"

namespace uw {
namespace {

using Kind = AutomaticOrganizer::Event::Kind;

// How long the card stays after a move, and after any other answer.
constexpr base::TimeDelta kChangeDuration = base::Seconds(6);
constexpr base::TimeDelta kAnswerDuration = base::Seconds(8);
// Longer than a request can take, so the card cannot outlive its request.
constexpr base::TimeDelta kAskingDuration = kProviderTimeout + base::Seconds(5);
constexpr int kRowSpacing = 4;
constexpr char kSettingsSubPage[] = "uwOrganization";

}  // namespace

OrganizeBar::OrganizeBar(BrowserWindowInterface* window) : window_(window) {
  SetOrientation(views::LayoutOrientation::kVertical);
  SetProperty(
      views::kMarginsKey,
      gfx::Insets::VH(kRowSpacing,
                      GetLayoutConstant(
                          LayoutConstant::kVerticalTabStripHorizontalPadding)));

  card_ = AddChildView(std::make_unique<ActivityCard>(
      base::BindRepeating(&OrganizeBar::Undo, base::Unretained(this))));
  card_->Hide();

  button_ = AddChildView(std::make_unique<views::MdTextButton>(
      base::BindRepeating(&OrganizeBar::ShowMenu, base::Unretained(this)),
      l10n_util::GetStringUTF16(IDS_UW_ORGANIZE)));
  StyleAsPill(button_);
  button_->SetProperty(views::kCrossAxisAlignmentKey,
                       views::LayoutAlignment::kStart);
  button_->SetProperty(views::kMarginsKey,
                       gfx::Insets::TLBR(kRowSpacing, 0, 0, 0));

  if (AutomaticOrganizer* organizer = AutomaticOrganizer::From(window_)) {
    event_subscription_ = organizer->ObserveEvents(base::BindRepeating(
        &OrganizeBar::OnOrganizerEvent, base::Unretained(this)));
  }
  if (TabOrganizationController* controller =
          TabOrganizationController::From(window_)) {
    manual_move_subscription_ =
        controller->ObserveManualMoves(base::BindRepeating(
            [](OrganizeBar* bar, const std::vector<PageId>&) {
              if (bar->card_->offers_undo()) {
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

OrganizeBar::~OrganizeBar() = default;

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

bool OrganizeBar::IsCommandIdChecked(int command_id) const {
  return command_id == kShowActivity && ShowsActivityDetails();
}

bool OrganizeBar::IsCommandIdEnabled(int command_id) const {
  const PauseMode pause = ReadPauseState(window_->GetProfile()->GetPrefs());
  switch (command_id) {
    case kUndo: {
      const TabOrganizationController* controller =
          TabOrganizationController::From(window_);
      return controller && controller->CanUndo();
    }
    case kPauseForAnHour:
      return pause != PauseMode::kTimed;
    case kPauseUntilLaunch:
      return pause != PauseMode::kUntilLaunch;
    case kPauseIndefinitely:
      return pause != PauseMode::kIndefinite;
    case kResume:
      return pause != PauseMode::kOff;
    default:
      return true;
  }
}

void OrganizeBar::ExecuteCommand(int command_id, int event_flags) {
  PrefService* prefs = window_->GetProfile()->GetPrefs();
  switch (command_id) {
    case kNewTask:
      TabOrganizationController::OpenNewTask(window_);
      break;
    case kUndo:
      Undo();
      break;
    case kPauseForAnHour:
      SetPauseState(prefs, PauseMode::kTimed, base::Hours(1));
      break;
    case kPauseUntilLaunch:
      SetPauseState(prefs, PauseMode::kUntilLaunch);
      break;
    case kPauseIndefinitely:
      SetPauseState(prefs, PauseMode::kIndefinite);
      break;
    case kResume:
      SetPauseState(prefs, PauseMode::kOff);
      break;
    case kShowActivity:
      prefs->SetBoolean(kOrganizationShowActivityPref, !ShowsActivityDetails());
      break;
    case kSettings:
      chrome::ShowSettingsSubPage(window_, kSettingsSubPage);
      break;
  }
}

void OrganizeBar::OnCollapseChanged(tabs::VerticalTabStripCollapseState state) {
  SetVisible(state == tabs::VerticalTabStripCollapseState::kExpanded);
}

void OrganizeBar::ShowMenu() {
  menu_model_ = std::make_unique<ui::SimpleMenuModel>(this);
  menu_model_->AddItemWithStringId(kNewTask, IDS_UW_OPEN_NEW_TASK);
  menu_model_->AddItemWithStringId(kUndo, IDS_UW_UNDO_ORGANIZATION);
  menu_model_->AddSeparator(ui::NORMAL_SEPARATOR);
  menu_model_->AddItemWithStringId(kPauseForAnHour, IDS_UW_PAUSE_1HOUR);
  menu_model_->AddItemWithStringId(kPauseUntilLaunch,
                                   IDS_UW_PAUSE_UNTIL_LAUNCH);
  menu_model_->AddItemWithStringId(kPauseIndefinitely, IDS_UW_PAUSE_INDEFINITE);
  menu_model_->AddItemWithStringId(kResume, IDS_UW_PAUSE_RESUME);
  menu_model_->AddSeparator(ui::NORMAL_SEPARATOR);
  menu_model_->AddCheckItemWithStringId(kShowActivity, IDS_UW_SHOW_ACTIVITY);
  menu_model_->AddItemWithStringId(kSettings, IDS_UW_ORGANIZATION_SETTINGS);

  hover_lock_ =
      BrowserView::GetBrowserViewForBrowser(window_)
          ->tab_strip_view()
          ->GetExpandOnHoverLock(ExpandOnHoverLockType::kKeepCurrentState);
  menu_runner_ = std::make_unique<views::MenuRunner>(
      menu_model_.get(), views::MenuRunner::HAS_MNEMONICS,
      base::BindRepeating(&OrganizeBar::OnMenuClosed, base::Unretained(this)));
  menu_runner_->RunMenuAt(GetWidget(), nullptr, button_->GetBoundsInScreen(),
                          views::MenuAnchorPosition::kTopLeft,
                          ui::mojom::MenuSourceType::kNone);
}

void OrganizeBar::OnMenuClosed() {
  hover_lock_.reset();
}

bool OrganizeBar::ShowsActivityDetails() const {
  return window_->GetProfile()->GetPrefs()->GetBoolean(
      kOrganizationShowActivityPref);
}

// A request on its way and a change that can be undone always show. What
// else a model answered shows only with "Show organizer activity" on.
void OrganizeBar::OnOrganizerEvent(const AutomaticOrganizer::Event& event) {
  const bool asking =
      event.kind == Kind::kAsking || event.kind == Kind::kNaming;
  const bool changed =
      event.kind == Kind::kMoved || event.kind == Kind::kCreated;
  if (!asking && !changed && !ShowsActivityDetails()) {
    HideCard();
    return;
  }
  card_->Show(event);
  card_timer_.Start(
      FROM_HERE,
      asking    ? kAskingDuration
      : changed ? kChangeDuration
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
