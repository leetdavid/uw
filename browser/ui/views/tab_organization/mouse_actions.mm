// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/browser/ui/views/tab_organization/mouse_actions.h"

#import <AppKit/AppKit.h>

#include <memory>
#include <optional>
#include <string>

#include "base/functional/bind.h"
#include "chrome/app/chrome_command_ids.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/color/chrome_color_id.h"
#include "chrome/browser/ui/tabs/vertical_tab_strip_state_controller.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/grit/generated_resources.h"
#include "components/prefs/pref_service.h"
#include "components/tabs/public/tab_interface.h"
#include "components/vector_icons/vector_icons.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/models/image_model.h"
#include "ui/color/color_provider.h"
#include "ui/events/event.h"
#include "ui/gfx/color_utils.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/animation/ink_drop.h"
#include "ui/views/animation/ink_drop_host.h"
#include "ui/views/controls/focus_ring.h"
#include "ui/views/controls/highlight_path_generator.h"
#include "ui/views/view_utils.h"
#include "ui/views/widget/widget.h"
#include "uw/browser/tab_organization/ai_connection.h"
#include "uw/browser/tab_organization/tab_organization_controller.h"
#include "uw/browser/ui/views/command_bar/command_bar.h"

namespace uw {
namespace {

constexpr int kIconSize = 16;

BrowserView* WindowOf(const views::View* view) {
  const views::Widget* widget = view->GetWidget();
  return widget ? BrowserView::GetBrowserViewForNativeWindow(
                      widget->GetPrimaryWindowWidget()->GetNativeWindow())
                : nullptr;
}

bool IsOn(BrowserWindowInterface* window, const char* pref) {
  return window && window->GetProfile()->GetPrefs()->GetBoolean(pref);
}

bool IsWide(BrowserWindowInterface* window) {
  const tabs::VerticalTabStripStateController* sidebar =
      tabs::VerticalTabStripStateController::From(window);
  return sidebar && !sidebar->IsCollapsed();
}

// True while the second click of a double-click is down or coming up. Only
// the event macOS is handing out says so: the window asks where the click
// goes before any view sees it.
bool IsSecondClick() {
  NSEvent* event = NSApp.currentEvent;
  return (event.type == NSEventTypeLeftMouseDown ||
          event.type == NSEventTypeLeftMouseUp) &&
         event.clickCount == 2;
}

}  // namespace

bool CloseBranchOnOptionClick(const views::View* row,
                              const tabs::TabInterface* tab,
                              const ui::Event& event) {
  BrowserView* window = WindowOf(row);
  if (!tab || !window || !(event.flags() & ui::EF_ALT_DOWN)) {
    return false;
  }
  TabOrganizationController* controller =
      TabOrganizationController::From(window->browser());
  const std::optional<PageId> id =
      controller ? controller->IdFor(tab) : std::nullopt;
  if (!id || !IsOn(window->browser(), kOptionClickClosesBranchPref) ||
      !controller->CanExecute(TabOrganizationController::kCloseBranch, {*id})) {
    return false;
  }
  controller->CloseBranches({*id});
  return true;
}

bool TakesMiddleClick(const views::View* row, const ui::MouseEvent& event) {
  BrowserView* window = WindowOf(row);
  return event.IsOnlyMiddleMouseButton() && window &&
         IsOn(window->browser(), kMiddleClickClosesTaskPref);
}

bool CloseTaskOnMiddleClick(views::View* row,
                            const tab_groups::TabGroupId& task,
                            const ui::MouseEvent& event) {
  if (!TakesMiddleClick(row, event)) {
    return false;
  }
  TabOrganizationController* controller =
      TabOrganizationController::From(WindowOf(row)->browser());
  if (controller && row->HitTestPoint(event.location())) {
    controller->CloseTask(task);
  }
  return true;
}

bool TakesDoubleClickBelowTabs(BrowserWindowInterface* window) {
  return IsOn(window, kDoubleClickOpensTabPref) && IsWide(window) &&
         IsSecondClick();
}

bool OpenTabOnDoubleClick(BrowserWindowInterface* window,
                          const ui::MouseEvent& event) {
  // The views never saw the first click, which dragged the window, so they
  // count this one as a single click. macOS's count says otherwise.
  if (!event.IsOnlyLeftMouseButton() || !TakesDoubleClickBelowTabs(window)) {
    return false;
  }
  chrome::ExecuteCommand(window, IDC_NEW_TAB);
  return true;
}

TaskRowNewTabButton::TaskRowNewTabButton(const tab_groups::TabGroupId& task)
    : LabelButton(base::BindRepeating(&TaskRowNewTabButton::Open,
                                      base::Unretained(this))),
      task_(task) {
  const std::u16string hint = l10n_util::GetStringUTF16(IDS_UW_NEW_TAB_IN_TASK);
  SetTooltipText(hint);
  GetViewAccessibility().SetName(hint);
  // The look of the row's menu button.
  SetHasInkDropActionOnClick(true);
  auto highlight =
      std::make_unique<views::CircleHighlightPathGenerator>(gfx::Insets());
  highlight->set_use_contents_bounds(true);
  views::HighlightPathGenerator::Install(this, std::move(highlight));
  views::InkDrop::Get(this)->SetMode(views::InkDropHost::InkDropMode::ON);
  views::InkDrop::Get(this)->SetHighlightOpacity(0.2f);
  views::InkDrop::Get(this)->SetVisibleOpacity(0.08f);
  SetFocusBehavior(FocusBehavior::ACCESSIBLE_ONLY);
  views::FocusRing::Install(this);
  SetVisible(false);
}

TaskRowNewTabButton::~TaskRowNewTabButton() = default;

// static
void TaskRowNewTabButton::ShowWithMenu(views::View* row, bool menu_shown) {
  BrowserView* window = WindowOf(row);
  const bool shown = menu_shown && window &&
                     IsOn(window->browser(), kTaskRowNewTabPref) &&
                     IsWide(window->browser());
  for (views::View* child : row->children()) {
    if (views::IsViewClass<TaskRowNewTabButton>(child)) {
      child->SetVisible(shown);
    }
  }
}

void TaskRowNewTabButton::OnThemeChanged() {
  LabelButton::OnThemeChanged();
  const SkColor color =
      GetColorProvider()->GetColor(kColorTabForegroundInactiveFrameActive);
  views::InkDrop::Get(this)->SetBaseColor(
      color_utils::GetColorWithMaxContrast(color));
  SetImageModel(STATE_NORMAL, ui::ImageModel::FromVectorIcon(
                                  vector_icons::kAdd2Icon, color, kIconSize));
}

void TaskRowNewTabButton::Open() {
  BrowserView* window = WindowOf(this);
  if (!window || CommandBar::OpenForTask(window->browser(), task_)) {
    return;
  }
  if (TabOrganizationController* controller =
          TabOrganizationController::From(window->browser())) {
    controller->OpenInTask(task_);
  }
}

BEGIN_METADATA(TaskRowNewTabButton)
END_METADATA

}  // namespace uw
