// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#ifndef UW_BROWSER_UI_VIEWS_TAB_ORGANIZATION_ORGANIZE_BAR_H_
#define UW_BROWSER_UI_VIEWS_TAB_ORGANIZATION_ORGANIZE_BAR_H_

#include <memory>
#include <string_view>

#include "base/callback_list.h"
#include "base/memory/raw_ptr.h"
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
class Label;
class MdTextButton;
class MenuRunner;
}  // namespace views

namespace uw {

// Sits at the foot of the tab sidebar. Holds the Organize button with its
// menu and, for a few seconds after each automatic change, a message that
// names the task with an Undo button. It never takes focus by itself.
class OrganizeBar : public views::FlexLayoutView,
                    public ui::SimpleMenuModel::Delegate {
  METADATA_HEADER(OrganizeBar, views::FlexLayoutView)

 public:
  enum Command {
    kUndo = 1,
    kPauseForAnHour,
    kPauseUntilLaunch,
    kPauseIndefinitely,
    kResume,
    kSettings,
  };

  explicit OrganizeBar(BrowserWindowInterface* window);
  OrganizeBar(const OrganizeBar&) = delete;
  OrganizeBar& operator=(const OrganizeBar&) = delete;
  ~OrganizeBar() override;

  bool notice_visible() const;
  std::u16string_view notice_text() const;
  void UndoNotice();

  // ui::SimpleMenuModel::Delegate:
  bool IsCommandIdEnabled(int command_id) const override;
  void ExecuteCommand(int command_id, int event_flags) override;

 private:
  void OnCollapseChanged(tabs::VerticalTabStripCollapseState state);
  void ShowMenu();
  void OnMenuClosed();
  void ShowNotice(const AutomaticOrganizer::Notice& notice);
  void HideNotice();

  const raw_ptr<BrowserWindowInterface> window_;
  raw_ptr<views::View> notice_ = nullptr;
  raw_ptr<views::Label> notice_label_ = nullptr;
  raw_ptr<views::MdTextButton> button_ = nullptr;
  std::unique_ptr<ui::SimpleMenuModel> menu_model_;
  std::unique_ptr<views::MenuRunner> menu_runner_;
  std::unique_ptr<ExpandOnHoverLock> hover_lock_;
  base::OneShotTimer notice_timer_;
  base::CallbackListSubscription collapse_subscription_;
  base::CallbackListSubscription notice_subscription_;
  base::CallbackListSubscription manual_move_subscription_;
};

}  // namespace uw

#endif  // UW_BROWSER_UI_VIEWS_TAB_ORGANIZATION_ORGANIZE_BAR_H_
