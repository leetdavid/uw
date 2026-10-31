// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/browser/ui/views/tab_organization/mouse_actions.h"

#import <AppKit/AppKit.h>

#include <memory>
#include <optional>
#include <string>

#include "base/functional/bind.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "chrome/app/chrome_command_ids.h"
#include "chrome/app/vector_icons/vector_icons.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/color/chrome_color_id.h"
#include "chrome/browser/ui/tabs/tab_group_model.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/grit/generated_resources.h"
#include "components/prefs/pref_service.h"
#include "components/tabs/public/tab_group.h"
#include "components/tabs/public/tab_interface.h"
#include "components/vector_icons/vector_icons.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/models/image_model.h"
#include "ui/base/ui_base_features.h"
#include "ui/color/color_provider.h"
#include "ui/events/event.h"
#include "ui/gfx/color_utils.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/animation/ink_drop.h"
#include "ui/views/animation/ink_drop_host.h"
#include "ui/views/background.h"
#include "ui/views/controls/focus_ring.h"
#include "ui/views/controls/highlight_path_generator.h"
#include "ui/views/view_utils.h"
#include "ui/views/widget/widget.h"
#include "uw/browser/tab_organization/ai_connection.h"
#include "uw/browser/tab_organization/tab_organization_controller.h"
#include "uw/browser/ui/views/command_bar/command_bar.h"
#include "uw/browser/ui/views/tab_organization/key_hint.h"

namespace uw {
namespace {

constexpr int kIconSize = 16;
// The ×, the size of a tab's.
constexpr int kCloseIconSize = 14;
// Chromium's task row's corners.
constexpr int kRowRadius = 8;
// The close color, under the rows a click on the × closes and on the × under
// the pointer.
constexpr SkAlpha kRowTint = 0x1A;
constexpr float kCloseHighlight = 0.14f;

BrowserView* WindowOf(const views::View* view) {
  const views::Widget* widget = view->GetWidget();
  return widget ? BrowserView::GetBrowserViewForNativeWindow(
                      widget->GetPrimaryWindowWidget()->GetNativeWindow())
                : nullptr;
}

bool IsOn(BrowserWindowInterface* window, const char* pref) {
  return window && window->GetProfile()->GetPrefs()->GetBoolean(pref);
}

// The look of the row's menu button.
void LikeTheMenuButton(views::LabelButton* button) {
  button->SetHasInkDropActionOnClick(true);
  auto highlight =
      std::make_unique<views::CircleHighlightPathGenerator>(gfx::Insets());
  highlight->set_use_contents_bounds(true);
  views::HighlightPathGenerator::Install(button, std::move(highlight));
  views::InkDrop::Get(button)->SetMode(views::InkDropHost::InkDropMode::ON);
  views::InkDrop::Get(button)->SetHighlightOpacity(0.2f);
  views::InkDrop::Get(button)->SetVisibleOpacity(0.08f);
  button->SetFocusBehavior(views::View::FocusBehavior::ACCESSIBLE_ONLY);
  views::FocusRing::Install(button);
  button->SetVisible(false);
}

template <typename T>
T* ChildOfClass(const views::View* row) {
  for (views::View* child : row->children()) {
    if (T* found = views::AsViewClass<T>(child)) {
      return found;
    }
  }
  return nullptr;
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
  return IsOn(window, kDoubleClickOpensTabPref) && IsSecondClick();
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
  LikeTheMenuButton(this);
}

TaskRowNewTabButton::~TaskRowNewTabButton() = default;

void ShowTaskRowButtons(views::View* row, bool menu_shown) {
  BrowserView* window = WindowOf(row);
  if (auto* plus = ChildOfClass<TaskRowNewTabButton>(row)) {
    plus->SetVisible(menu_shown && window &&
                     IsOn(window->browser(), kTaskRowNewTabPref));
  }
  if (auto* close = ChildOfClass<TaskRowCloseButton>(row)) {
    close->SetVisible(menu_shown);
  }
}

SkColor TaskRowFill(const views::View* row) {
  const TaskRowCloseButton* close = ChildOfClass<TaskRowCloseButton>(row);
  BrowserView* window = WindowOf(row);
  return close && window && IsTaskCloseHinted(window->browser(), close->task())
             ? SkColorSetA(CloseHintColor(row), kRowTint)
             : SK_ColorTRANSPARENT;
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

TaskRowCloseButton::TaskRowCloseButton(const tab_groups::TabGroupId& task)
    : LabelButton(base::BindRepeating(&TaskRowCloseButton::Close,
                                      base::Unretained(this))),
      task_(task) {
  GetViewAccessibility().SetName(u"Close task");
  LikeTheMenuButton(this);
  views::InkDrop::Get(this)->SetHighlightOpacity(kCloseHighlight);
}

TaskRowCloseButton::~TaskRowCloseButton() = default;

void TaskRowCloseButton::AddedToWidget() {
  LabelButton::AddedToWidget();
  hints_ = ObserveCloseHints(base::BindRepeating(&TaskRowCloseButton::UpdateRow,
                                                 base::Unretained(this)));
}

void TaskRowCloseButton::RemovedFromWidget() {
  hints_ = {};
  if (BrowserView* window = WindowOf(this)) {
    SetTaskCloseHint(false, window->browser(), task_, false);
  }
  LabelButton::RemovedFromWidget();
}

void TaskRowCloseButton::VisibilityChanged(views::View* starting_from,
                                           bool is_visible) {
  LabelButton::VisibilityChanged(starting_from, is_visible);
  UpdateHint();
}

void TaskRowCloseButton::OnThemeChanged() {
  LabelButton::OnThemeChanged();
  const SkColor color =
      GetColorProvider()->GetColor(kColorTabForegroundInactiveFrameActive);
  const SkColor close = CloseHintColor(this);
  views::InkDrop::Get(this)->SetBaseColor(close);
  const gfx::VectorIcon& icon = features::IsRoundedIconsEnabled()
                                    ? kCloseWeight500CustomIcon
                                    : kCloseTabChromeRefreshOldIcon;
  SetImageModel(STATE_NORMAL,
                ui::ImageModel::FromVectorIcon(icon, color, kCloseIconSize));
  for (ButtonState state : {STATE_HOVERED, STATE_PRESSED}) {
    SetImageModel(state,
                  ui::ImageModel::FromVectorIcon(icon, close, kCloseIconSize));
  }
  UpdateRow();
}

void TaskRowCloseButton::StateChanged(ButtonState old_state) {
  LabelButton::StateChanged(old_state);
  UpdateHint();
}

std::u16string TaskRowCloseButton::GetRenderedTooltipText(
    const gfx::Point& point) const {
  BrowserView* window = WindowOf(this);
  TabStripModel* tabs = window ? window->browser()->tab_strip_model() : nullptr;
  if (!tabs || !tabs->group_model() ||
      !tabs->group_model()->ContainsTabGroup(task_)) {
    return std::u16string();
  }
  const int count = tabs->group_model()->GetTabGroup(task_)->tab_count();
  return count == 1 ? u"Close task and its tab"
                    : base::StrCat({u"Close task and its ",
                                    base::NumberToString16(count), u" tabs"});
}

void TaskRowCloseButton::Close() {
  BrowserView* window = WindowOf(this);
  if (TabOrganizationController* controller =
          window ? TabOrganizationController::From(window->browser())
                 : nullptr) {
    controller->CloseTask(task_);
  }
}

void TaskRowCloseButton::UpdateHint() {
  if (BrowserView* window = WindowOf(this)) {
    SetTaskCloseHint(false, window->browser(), task_,
                     IsDrawn() && (GetState() == STATE_HOVERED ||
                                   GetState() == STATE_PRESSED));
  }
}

void TaskRowCloseButton::UpdateRow() {
  if (GetWidget()) {
    parent()->SetBackground(
        views::CreateRoundedRectBackground(TaskRowFill(parent()), kRowRadius));
  }
}

BEGIN_METADATA(TaskRowCloseButton)
END_METADATA

}  // namespace uw
