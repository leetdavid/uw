// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#ifndef UW_BROWSER_UI_VIEWS_TAB_ORGANIZATION_ORGANIZE_BAR_H_
#define UW_BROWSER_UI_VIEWS_TAB_ORGANIZATION_ORGANIZE_BAR_H_

#include <memory>
#include <string>
#include <string_view>

#include "base/callback_list.h"
#include "base/memory/raw_ptr.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/menus/simple_menu_model.h"
#include "ui/views/context_menu_controller.h"
#include "ui/views/layout/flex_layout_view.h"
#include "ui/views/widget/widget.h"
#include "uw/browser/tab_organization/automatic_organizer.h"
#include "uw/browser/tab_organization/commands.h"
#include "uw/browser/tab_organization/tab_organization_controller.h"

class BrowserWindowInterface;
class ExpandOnHoverLock;

namespace views {
class BubbleDialogDelegate;
class Button;
class ImageButton;
class MenuRunner;
}  // namespace views

namespace uw {

class ActivityCard;
class NewButton;
class ThemePicker;

// Sits at the foot of the tab sidebar. Holds the Organize button with its
// card at the left end, the new button at the right end, and the workspace
// switcher between them. Above them, a card names the
// task after each automatic move, with an Undo button, and says when a
// request fails. With "Show organizer activity" on, the card also shows each
// request and what every answer was. It never takes focus by itself.
class OrganizeBar : public views::FlexLayoutView,
                    public ui::SimpleMenuModel::Delegate,
                    public views::ContextMenuController {
  METADATA_HEADER(OrganizeBar, views::FlexLayoutView)

 public:
  // The window commands of the menu bar.
  enum Command {
    kUndo = TabOrganizationController::kUndo,
    kPauseForAnHour = kPauseOrganizationForAnHour,
    kPauseUntilLaunch = kPauseOrganizationUntilLaunch,
    kPauseIndefinitely = kPauseOrganizationIndefinitely,
    kResume = kResumeOrganization,
    kShowActivity = kShowOrganizerActivity,
    kSettings = kShowOrganizationSettings,
    kTheme = kShowThemePicker,
  };

  explicit OrganizeBar(BrowserWindowInterface* window);
  OrganizeBar(const OrganizeBar&) = delete;
  OrganizeBar& operator=(const OrganizeBar&) = delete;
  ~OrganizeBar() override;

  bool card_visible() const;
  const ActivityCard* card() const { return card_; }
  NewButton* new_button() { return new_button_; }
  views::Button* organize_button();
  // Puts `middle`, such as the workspace switcher, between Organize and the
  // new button.
  void SetMiddle(std::unique_ptr<views::View> middle);
  // The Organize card while it is open.
  views::Widget* organize_card() { return card_widget_.get(); }
  void ShowMenu();
  // Reverses the last organization action and puts the card away.
  void Undo();
  // Carries out the suggestion the card shows.
  void AcceptSuggestion();
  // Try Again after a failed Name Task.
  void AskForNameAgain(const std::string& task);
  void TidyAgain();

  // ui::SimpleMenuModel::Delegate:
  bool IsCommandIdChecked(int command_id) const override;
  bool IsCommandIdEnabled(int command_id) const override;
  void ExecuteCommand(int command_id, int event_flags) override;
  bool GetAcceleratorForCommandId(int command_id,
                                  ui::Accelerator* accelerator) const override;

  // views::ContextMenuController, for empty space in the sidebar:
  void ShowContextMenuForViewImpl(
      views::View* source,
      const gfx::Point& point,
      ui::mojom::MenuSourceType source_type) override;

 private:
  void OnMenuClosed();
  void OnCardClosed(views::Widget::ClosedReason reason);
  bool ShowsActivityDetails() const;
  void OnOrganizerEvent(const AutomaticOrganizer::Event& event);
  void HideCard();

  const raw_ptr<BrowserWindowInterface> window_;
  raw_ptr<ActivityCard> card_ = nullptr;
  raw_ptr<views::ImageButton> button_ = nullptr;
  raw_ptr<views::FlexLayoutView> row_ = nullptr;
  raw_ptr<views::View> spacer_ = nullptr;
  raw_ptr<NewButton> new_button_ = nullptr;
  std::unique_ptr<ui::SimpleMenuModel> menu_model_;
  std::unique_ptr<views::MenuRunner> menu_runner_;
  // The Organize card's bubble outlives its widget.
  std::unique_ptr<views::BubbleDialogDelegate> card_bubble_;
  std::unique_ptr<views::Widget> card_widget_;
  std::unique_ptr<ThemePicker> theme_picker_;
  std::unique_ptr<ExpandOnHoverLock> hover_lock_;
  base::OneShotTimer card_timer_;
  // The last answer was a failed request, so the next failure stays quiet.
  bool failing_ = false;
  base::CallbackListSubscription event_subscription_;
  base::CallbackListSubscription manual_move_subscription_;
};

}  // namespace uw

#endif  // UW_BROWSER_UI_VIEWS_TAB_ORGANIZATION_ORGANIZE_BAR_H_
