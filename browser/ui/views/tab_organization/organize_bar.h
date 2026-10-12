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
#include "ui/views/layout/flex_layout_view.h"
#include "uw/browser/tab_organization/automatic_organizer.h"

class BrowserWindowInterface;
class ExpandOnHoverLock;

namespace tabs {
enum class VerticalTabStripCollapseState;
}

namespace views {
class MdTextButton;
class MenuRunner;
}  // namespace views

namespace uw {

class ActivityCard;

// Sits at the foot of the tab sidebar. Holds the Organize button with its
// menu. Above it, a card shows when a model is being asked about a tab, and
// names the task after each automatic move, with an Undo button. With "Show
// organizer activity" on, the card also shows what every answer was. It never
// takes focus by itself.
class OrganizeBar : public views::FlexLayoutView,
                    public ui::SimpleMenuModel::Delegate {
  METADATA_HEADER(OrganizeBar, views::FlexLayoutView)

 public:
  enum Command {
    kNewTask = 1,
    kUndo,
    kPauseForAnHour,
    kPauseUntilLaunch,
    kPauseIndefinitely,
    kResume,
    kShowActivity,
    kSettings,
  };

  explicit OrganizeBar(BrowserWindowInterface* window);
  OrganizeBar(const OrganizeBar&) = delete;
  OrganizeBar& operator=(const OrganizeBar&) = delete;
  ~OrganizeBar() override;

  bool card_visible() const;
  const ActivityCard* card() const { return card_; }
  // Reverses the last organization action and puts the card away.
  void Undo();
  // Carries out the suggestion the card shows.
  void AcceptSuggestion();

  // ui::SimpleMenuModel::Delegate:
  bool IsCommandIdChecked(int command_id) const override;
  bool IsCommandIdEnabled(int command_id) const override;
  void ExecuteCommand(int command_id, int event_flags) override;

 private:
  void OnCollapseChanged(tabs::VerticalTabStripCollapseState state);
  void ShowMenu();
  void OnMenuClosed();
  bool ShowsActivityDetails() const;
  void OnOrganizerEvent(const AutomaticOrganizer::Event& event);
  void HideCard();

  const raw_ptr<BrowserWindowInterface> window_;
  raw_ptr<ActivityCard> card_ = nullptr;
  raw_ptr<views::MdTextButton> button_ = nullptr;
  std::unique_ptr<ui::SimpleMenuModel> menu_model_;
  std::unique_ptr<views::MenuRunner> menu_runner_;
  std::unique_ptr<ExpandOnHoverLock> hover_lock_;
  base::OneShotTimer card_timer_;
  base::CallbackListSubscription collapse_subscription_;
  base::CallbackListSubscription event_subscription_;
  base::CallbackListSubscription manual_move_subscription_;
};

}  // namespace uw

#endif  // UW_BROWSER_UI_VIEWS_TAB_ORGANIZATION_ORGANIZE_BAR_H_
