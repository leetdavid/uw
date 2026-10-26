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
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/tab_strip_region_view.h"
#include "chrome/grit/generated_resources.h"
#include "components/prefs/pref_service.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/models/image_model.h"
#include "ui/base/mojom/menu_source_type.mojom.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/background.h"
#include "ui/views/bubble/bubble_dialog_delegate_view.h"
#include "ui/views/controls/button/image_button.h"
#include "ui/views/controls/highlight_path_generator.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/menu/menu_runner.h"
#include "ui/views/layout/flex_layout.h"
#include "ui/views/view_class_properties.h"
#include "uw/browser/ai_providers/models.h"
#include "uw/browser/tab_organization/ai_connection.h"
#include "uw/browser/tab_organization/tab_organization_controller.h"
#include "uw/browser/ui/glass/glass_background.h"
#include "uw/browser/ui/vector_icons/vector_icons.h"
#include "uw/browser/ui/views/mini_player/mini_player_dock.h"
#include "uw/browser/ui/views/mini_player/playback_controls.h"
#include "uw/browser/ui/views/tab_organization/activity_card.h"
#include "uw/browser/ui/views/tab_organization/new_button.h"
#include "uw/browser/ui/views/tab_organization/organize_card.h"
#include "uw/browser/ui/views/tab_organization/window_commands.h"
#include "uw/browser/ui/views/theme_picker/theme_picker.h"
#include "uw/browser/workspaces/workspace_theme.h"

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
// Organize is a pill with the tree icon alone, as tall as the new button.
constexpr int kOrganizeWidth = 32;
constexpr int kOrganizeHeight = 28;
constexpr int kOrganizeIconSize = 15;
constexpr int kFootGap = 6;

}  // namespace

OrganizeBar::OrganizeBar(BrowserWindowInterface* window)
    : window_(window), theme_picker_(std::make_unique<ThemePicker>(window)) {
  SetOrientation(views::LayoutOrientation::kVertical);
  // The card, the mini player, and the playback controls sit 6px apart and
  // 6px above the row: touching margins share their space.
  SetCollapseMargins(true);
  SetProperty(
      views::kMarginsKey,
      gfx::Insets::VH(kRowSpacing,
                      GetLayoutConstant(
                          LayoutConstant::kVerticalTabStripHorizontalPadding)));

  card_ = AddChildView(std::make_unique<ActivityCard>(
      GlassOfWindow(window_),
      base::BindRepeating(&OrganizeBar::Undo, base::Unretained(this)),
      base::BindRepeating(&OrganizeBar::AcceptSuggestion,
                          base::Unretained(this)),
      base::BindRepeating(&OrganizeBar::AskForNameAgain,
                          base::Unretained(this)),
      base::BindRepeating(&OrganizeBar::TidyAgain, base::Unretained(this))));
  card_->SetProperty(views::kMarginsKey, gfx::Insets::TLBR(0, 0, kFootGap, 0));
  card_->Hide();

  // The mini player docks between the card and the row, the playback
  // controls under it.
  AddChildView(std::make_unique<MiniPlayerDock>(window_));
  AddChildView(std::make_unique<PlaybackControls>(window_));

  // Organize at the left, the new button at the right end.
  row_ = AddChildView(std::make_unique<views::FlexLayoutView>());
  row_->SetOrientation(views::LayoutOrientation::kHorizontal);
  row_->SetCrossAxisAlignment(views::LayoutAlignment::kCenter);
  row_->SetProperty(views::kMarginsKey,
                    gfx::Insets::TLBR(kRowSpacing, 0, 0, 0));
  button_ = row_->AddChildView(std::make_unique<views::ImageButton>(
      base::BindRepeating(&OrganizeBar::ShowMenu, base::Unretained(this))));
  button_->SetPreferredSize(gfx::Size(kOrganizeWidth, kOrganizeHeight));
  button_->SetImageHorizontalAlignment(views::ImageButton::ALIGN_CENTER);
  button_->SetImageVerticalAlignment(views::ImageButton::ALIGN_MIDDLE);
  button_->SetImageModel(
      views::Button::STATE_NORMAL,
      ui::ImageModel::FromVectorIcon(kListTreeIcon,
                                     kColorTabForegroundInactiveFrameActive,
                                     kOrganizeIconSize));
  button_->SetBackground(CreateGlassPillBackground(
      GlassOfWindow(window_), kColorNewTabButtonCRBackgroundFrameActive,
      kOrganizeHeight / 2.0f));
  views::InstallPillHighlightPathGenerator(button_);
  const std::u16string organize = l10n_util::GetStringUTF16(IDS_UW_ORGANIZE);
  button_->SetTooltipText(organize);
  button_->GetViewAccessibility().SetName(organize);
  spacer_ = row_->AddChildView(std::make_unique<views::View>());
  spacer_->SetUseDefaultFillLayout(true);
  spacer_->SetProperty(views::kMarginsKey, gfx::Insets::VH(0, kFootGap));
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
}

OrganizeBar::~OrganizeBar() {
  card_widget_.reset();
}

views::Button* OrganizeBar::organize_button() {
  return button_;
}

void OrganizeBar::SetMiddle(std::unique_ptr<views::View> middle) {
  if (middle) {
    spacer_->AddChildView(std::move(middle));
  }
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

void OrganizeBar::TidyAgain() {
  HideCard();
  if (AutomaticOrganizer* organizer = AutomaticOrganizer::From(window_)) {
    organizer->Tidy();
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

void OrganizeBar::ShowMenu() {
  if (card_widget_) {
    return;
  }
  card_bubble_ = OrganizeCard::CreateBubble(button_, this, window_);
  card_widget_ = views::BubbleDialogDelegate::CreateBubble(
      card_bubble_.get(),
      base::BindOnce(&OrganizeBar::OnCardClosed, base::Unretained(this)));
  ShowInThemeMode(card_widget_.get(), window_);
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
  ShowOpenMenuInThemeMode(window_);
}

void OrganizeBar::OnMenuClosed() {
  hover_lock_.reset();
}

bool OrganizeBar::ShowsActivityDetails() const {
  return window_->GetProfile()->GetPrefs()->GetBoolean(
      kOrganizationShowActivityPref);
}

// A change that can be undone always shows, and so do a failed Name Task
// and every step of Tidy, which the user asked for, and the first failed
// request after one that worked. The rest, and the model's numbers, show only
// with Show Activity on.
void OrganizeBar::OnOrganizerEvent(const AutomaticOrganizer::Event& event) {
  const bool asking = event.kind == Kind::kAsking ||
                      event.kind == Kind::kNaming ||
                      event.kind == Kind::kTidying;
  const bool changed = event.kind == Kind::kMoved ||
                       event.kind == Kind::kCreated ||
                       event.kind == Kind::kTidied;
  const bool failed = event.kind == Kind::kRequestFailed;
  const bool first_failure = failed && !failing_;
  const bool asked =
      event.kind == Kind::kNameFailed || event.kind == Kind::kTidying ||
      event.kind == Kind::kTidyNothing || event.kind == Kind::kTidyFailed;
  if (!asking && !asked) {
    failing_ = failed;
  }
  const bool details = ShowsActivityDetails();
  if (!details && !changed && !first_failure && !asked) {
    return;
  }
  card_->Show(event, details);
  card_timer_.Start(
      FROM_HERE,
      asking                                 ? kAskingDuration
      : changed                              ? kChangeDuration
      : event.kind == Kind::kUnsure || asked ? kSuggestionDuration
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
