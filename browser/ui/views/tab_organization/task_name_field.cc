// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/browser/ui/views/tab_organization/task_name_field.h"

#include <utility>

#include "base/auto_reset.h"
#include "base/functional/bind.h"
#include "base/strings/string_util.h"
#include "base/task/sequenced_task_runner.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_group_model.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/tab_strip_region_view.h"
#include "chrome/browser/ui/views/tabs/vertical/vertical_tab_strip_expand_on_hover_lock.h"
#include "chrome/grit/generated_resources.h"
#include "components/tab_groups/tab_group_visual_data.h"
#include "components/tabs/public/tab_group.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/events/event.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/views/border.h"
#include "ui/views/controls/focus_ring.h"
#include "ui/views/controls/label.h"
#include "ui/views/focus/focus_manager.h"
#include "ui/views/layout/flex_layout_types.h"
#include "ui/views/view_class_properties.h"
#include "ui/views/widget/widget.h"
#include "uw/browser/tab_organization/automatic_organizer.h"
#include "uw/browser/tab_organization/tab_organization_controller.h"
#include "uw/browser/ui/look/menu_card.h"

namespace uw {
namespace {

constexpr int kRingWidth = 2;
constexpr float kRadius = 8;
constexpr gfx::Insets kTextInsets = gfx::Insets::VH(2, 5);

BrowserView* WindowOf(const views::View* view) {
  const views::Widget* widget = view->GetWidget();
  return widget ? BrowserView::GetBrowserViewForNativeWindow(
                      widget->GetPrimaryWindowWidget()->GetNativeWindow())
                : nullptr;
}

}  // namespace

TaskNameField::TaskNameField(const tab_groups::TabGroupId& group,
                             views::Label* name)
    : group_(group), name_(name) {
  SetController(this);
  SetVisible(false);
  SetFontList(name->font_list());
  SetTextColorId(kColorMenuCardText);
  SetBackgroundColor(kColorMenuCardBackground);
  SetBorder(views::CreatePaddedBorder(
      views::CreateRoundedRectBorder(kRingWidth, kRadius, kColorMenuCardAccent),
      kTextInsets));
  SetAccessibleName(l10n_util::GetStringUTF16(
      IDS_TAB_GROUP_HEADER_CXMENU_TAB_GROUP_TITLE_ACCESSIBLE_NAME));
  SetProperty(views::kFlexBehaviorKey,
              views::FlexSpecification(views::LayoutOrientation::kHorizontal,
                                       views::MinimumFlexSizeRule::kScaleToZero,
                                       views::MaximumFlexSizeRule::kUnbounded));
}

TaskNameField::~TaskNameField() = default;

// static
TaskNameField* TaskNameField::On(views::View* row) {
  for (views::View* child : row->children()) {
    if (TaskNameField* field = views::AsViewClass<TaskNameField>(child)) {
      return field;
    }
  }
  return nullptr;
}

void TaskNameField::Start() {
  if (editing() || !GetWidget()) {
    return;
  }
  if (BrowserView* window = WindowOf(this)) {
    if (AutomaticOrganizer* organizer =
            AutomaticOrganizer::From(window->browser())) {
      organizer->DropAskedName(group_.ToString());
    }
    expand_lock_ = window->tab_strip_view()->GetExpandOnHoverLock(
        ExpandOnHoverLockType::kKeepExpanded);
  }
  views::FocusRing::Remove(this);
  previous_focus_.SetView(GetFocusManager()->GetFocusedView());
  started_with_ = std::u16string(name_->GetText());
  SetText(started_with_);
  name_->SetVisible(false);
  SetVisible(true);
  SelectAll(false);
  RequestFocus();
}

void TaskNameField::AddedToWidget() {
  Textfield::AddedToWidget();
  TabOrganizationController* controller = Controller();
  if (!controller) {
    return;
  }
  renames_ = controller->ObserveRenames(base::BindRepeating(
      &TaskNameField::OnRenameRequested, base::Unretained(this)));
  // A row shown for a task that asked for its name field starts with it once
  // the row has its place.
  if (controller->TakeRename(group_.ToString())) {
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE,
        base::BindOnce(&TaskNameField::Start, weak_factory_.GetWeakPtr()));
  }
}

void TaskNameField::RemovedFromWidget() {
  renames_ = {};
  expand_lock_.reset();
  Textfield::RemovedFromWidget();
}

void TaskNameField::OnBlur() {
  Textfield::OnBlur();
  End(/*save=*/true, /*restore_focus=*/false);
}

bool TaskNameField::HandleKeyEvent(views::Textfield* sender,
                                   const ui::KeyEvent& key_event) {
  if (key_event.type() != ui::EventType::kKeyPressed) {
    return false;
  }
  switch (key_event.key_code()) {
    case ui::VKEY_RETURN:
      End(/*save=*/true, /*restore_focus=*/true);
      return true;
    case ui::VKEY_ESCAPE:
      End(/*save=*/false, /*restore_focus=*/true);
      return true;
    default:
      return false;
  }
}

TabOrganizationController* TaskNameField::Controller() const {
  BrowserView* window = WindowOf(this);
  return window ? TabOrganizationController::From(window->browser()) : nullptr;
}

void TaskNameField::OnRenameRequested() {
  if (TabOrganizationController* controller = Controller();
      controller && controller->TakeRename(group_.ToString())) {
    Start();
  }
}

void TaskNameField::End(bool save, bool restore_focus) {
  if (!editing() || ending_) {
    return;
  }
  base::AutoReset<bool> ending(&ending_, true);
  const std::u16string typed(base::TrimWhitespace(GetText(), base::TRIM_ALL));
  // Focus moves before the field hides, or hiding it would move focus on to
  // the next row.
  if (restore_focus) {
    views::View* previous = previous_focus_.view();
    if (previous && previous != this && previous->IsDrawn()) {
      previous->RequestFocus();
    } else if (GetFocusManager()) {
      GetFocusManager()->ClearFocus();
    }
  }
  previous_focus_.SetView(nullptr);
  SetVisible(false);
  name_->SetVisible(true);
  expand_lock_.reset();
  // Only what the user typed is saved, so a name uw gave meanwhile stays.
  if (!save || typed.empty() || typed == started_with_) {
    return;
  }
  BrowserView* window = WindowOf(this);
  TabStripModel* tabs =
      window ? window->browser()->GetTabStripModel() : nullptr;
  if (!tabs || !tabs->SupportsTabGroups() ||
      !tabs->group_model()->ContainsTabGroup(group_)) {
    return;
  }
  const tab_groups::TabGroupVisualData* visuals =
      tabs->group_model()->GetTabGroup(group_)->visual_data();
  tabs->ChangeTabGroupVisuals(
      group_, tab_groups::TabGroupVisualData(typed, visuals->color(),
                                             visuals->is_collapsed()));
}

BEGIN_METADATA(TaskNameField)
END_METADATA

}  // namespace uw
