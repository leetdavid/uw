// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/browser/ui/views/tab_organization/organize_bar.h"

#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "base/time/time.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/chrome_pages.h"
#include "chrome/browser/ui/color/chrome_color_id.h"
#include "chrome/browser/ui/layout_constants.h"
#include "chrome/browser/ui/tabs/vertical_tab_strip_state_controller.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/tab_strip_region_view.h"
#include "chrome/grit/generated_resources.h"
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

namespace uw {
namespace {

constexpr base::TimeDelta kNoticeDuration = base::Seconds(6);
constexpr int kRowSpacing = 4;
constexpr char kSettingsSubPage[] = "uwOrganization";

// A filled pill in the colors of the window tint.
void StyleAsPill(views::MdTextButton* button) {
  button->SetBgColorIdOverride(kColorNewTabButtonCRBackgroundFrameActive);
  button->SetStrokeColorIdOverride(kColorNewTabButtonCRBackgroundFrameActive);
  button->SetEnabledTextColors(kColorTabForegroundInactiveFrameActive);
}

}  // namespace

OrganizeBar::OrganizeBar(BrowserWindowInterface* window) : window_(window) {
  SetOrientation(views::LayoutOrientation::kVertical);
  SetProperty(
      views::kMarginsKey,
      gfx::Insets::VH(kRowSpacing,
                      GetLayoutConstant(
                          LayoutConstant::kVerticalTabStripHorizontalPadding)));

  auto notice = std::make_unique<views::FlexLayoutView>();
  notice->SetCrossAxisAlignment(views::LayoutAlignment::kCenter);
  notice->SetVisible(false);
  notice_label_ = notice->AddChildView(std::make_unique<views::Label>());
  notice_label_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  notice_label_->SetEnabledColor(kColorTabForegroundInactiveFrameActive);
  notice_label_->SetBackgroundColor(SK_ColorTRANSPARENT);
  notice_label_->SetProperty(
      views::kFlexBehaviorKey,
      views::FlexSpecification(views::LayoutOrientation::kHorizontal,
                               views::MinimumFlexSizeRule::kScaleToZero,
                               views::MaximumFlexSizeRule::kUnbounded));
  StyleAsPill(notice->AddChildView(std::make_unique<views::MdTextButton>(
      base::BindRepeating(&OrganizeBar::UndoNotice, base::Unretained(this)),
      l10n_util::GetStringUTF16(IDS_UW_AUTO_UNDO))));
  notice_ = AddChildView(std::move(notice));

  button_ = AddChildView(std::make_unique<views::MdTextButton>(
      base::BindRepeating(&OrganizeBar::ShowMenu, base::Unretained(this)),
      l10n_util::GetStringUTF16(IDS_UW_ORGANIZE)));
  StyleAsPill(button_);
  button_->SetProperty(views::kCrossAxisAlignmentKey,
                       views::LayoutAlignment::kStart);
  button_->SetProperty(views::kMarginsKey,
                       gfx::Insets::TLBR(kRowSpacing, 0, 0, 0));

  if (AutomaticOrganizer* organizer = AutomaticOrganizer::From(window_)) {
    notice_subscription_ = organizer->ObserveNotices(
        base::BindRepeating(&OrganizeBar::ShowNotice, base::Unretained(this)));
  }
  if (TabOrganizationController* controller =
          TabOrganizationController::From(window_)) {
    manual_move_subscription_ =
        controller->ObserveManualMoves(base::BindRepeating(
            [](OrganizeBar* bar, const std::vector<PageId>&) {
              bar->HideNotice();
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

bool OrganizeBar::notice_visible() const {
  return notice_->GetVisible();
}

std::u16string_view OrganizeBar::notice_text() const {
  return notice_label_->GetText();
}

void OrganizeBar::UndoNotice() {
  if (TabOrganizationController* controller =
          TabOrganizationController::From(window_)) {
    controller->Undo();
  }
  HideNotice();
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
    case kUndo:
      UndoNotice();
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
  menu_model_->AddItemWithStringId(kUndo, IDS_UW_UNDO_ORGANIZATION);
  menu_model_->AddSeparator(ui::NORMAL_SEPARATOR);
  menu_model_->AddItemWithStringId(kPauseForAnHour, IDS_UW_PAUSE_1HOUR);
  menu_model_->AddItemWithStringId(kPauseUntilLaunch,
                                   IDS_UW_PAUSE_UNTIL_LAUNCH);
  menu_model_->AddItemWithStringId(kPauseIndefinitely, IDS_UW_PAUSE_INDEFINITE);
  menu_model_->AddItemWithStringId(kResume, IDS_UW_PAUSE_RESUME);
  menu_model_->AddSeparator(ui::NORMAL_SEPARATOR);
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

void OrganizeBar::ShowNotice(const AutomaticOrganizer::Notice& notice) {
  notice_label_->SetText(l10n_util::GetStringFUTF16(
      notice.created ? IDS_UW_AUTO_CREATED_TASK : IDS_UW_AUTO_MOVED_TASK,
      notice.task));
  notice_->SetVisible(true);
  notice_timer_.Start(
      FROM_HERE, kNoticeDuration,
      base::BindOnce(&OrganizeBar::HideNotice, base::Unretained(this)));
}

void OrganizeBar::HideNotice() {
  notice_timer_.Stop();
  notice_->SetVisible(false);
}

BEGIN_METADATA(OrganizeBar)
END_METADATA

}  // namespace uw
