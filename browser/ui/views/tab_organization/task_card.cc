// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/browser/ui/views/tab_organization/task_card.h"

#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "base/memory/weak_ptr.h"
#include "base/scoped_observation.h"
#include "base/task/sequenced_task_runner.h"
#include "cc/paint/paint_flags.h"
#include "chrome/app/chrome_command_ids.h"
#include "chrome/app/vector_icons/vector_icons.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/tabs/tab_group_model.h"
#include "chrome/browser/ui/tabs/tab_group_theme.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/tabs/tab_strip_model_delegate.h"
#include "chrome/grit/generated_resources.h"
#include "components/tab_groups/tab_group_visual_data.h"
#include "components/tabs/public/tab_group.h"
#include "components/vector_icons/vector_icons.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/base/accelerators/accelerator.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/color/color_provider.h"
#include "ui/gfx/canvas.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/bubble/bubble_dialog_delegate_view.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/widget/widget.h"
#include "ui/views/widget/widget_observer.h"
#include "url/gurl.h"
#include "uw/browser/tab_organization/automatic_organizer.h"
#include "uw/browser/tab_organization/commands.h"
#include "uw/browser/tab_organization/tab_organization_controller.h"
#include "uw/browser/ui/look/menu_card.h"
#include "uw/browser/ui/views/tab_organization/menu_card_row.h"
#include "uw/browser/ui/views/tab_organization/window_commands.h"
#include "uw/browser/workspaces/workspace_theme.h"

namespace uw {
namespace {

constexpr int kDotSize = 14;
constexpr int kDotSpacing = 9;
constexpr int kDotRingGap = 2;
constexpr int kDotRingWidth = 2;
constexpr int kDotsInset = 10 - kDotSpacing / 2;

// The bubble of a task card. The tab strip keeps the widget, so the bubble
// deletes itself once the widget is gone.
class TaskCardBubble : public views::BubbleDialogDelegate,
                       public views::WidgetObserver {
 public:
  explicit TaskCardBubble(views::View* row)
      : BubbleDialogDelegate(row, views::BubbleBorder::TOP_LEFT) {}

  void Watch(views::Widget* widget) { observation_.Observe(widget); }

  // views::WidgetObserver:
  void OnWidgetDestroyed(views::Widget* widget) override {
    observation_.Reset();
    // The row's window outlives the card, and would pass its theme changes
    // on to the gone card until this deletes.
    SetAnchorView(nullptr);
    base::SequencedTaskRunner::GetCurrentDefault()->DeleteSoon(FROM_HERE, this);
  }

 private:
  base::ScopedObservation<views::Widget, views::WidgetObserver> observation_{
      this};
};

// One of the task colors, ringed when it is the task's.
class ColorDot : public views::Button {
  METADATA_HEADER(ColorDot, views::Button)

 public:
  ColorDot(PressedCallback callback,
           tab_groups::TabGroupColorId color,
           bool current)
      : Button(std::move(callback)), color_(color), current_(current) {
    SetPreferredSize(gfx::Size(kDotSize + kDotSpacing, kMenuRowHeight));
    SetUpMenuCardFocus(this);
    GetViewAccessibility().SetRole(ax::mojom::Role::kRadioButton);
    GetViewAccessibility().SetName(
        tab_groups::GetTabGroupColorLabelMap().at(color));
    GetViewAccessibility().SetCheckedState(
        current ? ax::mojom::CheckedState::kTrue
                : ax::mojom::CheckedState::kFalse);
  }

  void PaintButtonContents(gfx::Canvas* canvas) override {
    const ui::ColorProvider* colors = GetColorProvider();
    const gfx::PointF center = gfx::RectF(GetLocalBounds()).CenterPoint();
    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    flags.setColor(colors->GetColor(
        GetTabGroupTabStripColorId(color_, /*active_frame=*/true)));
    canvas->DrawCircle(center, kDotSize / 2.0f, flags);
    if (!current_ && !HasFocus()) {
      return;
    }
    flags.setStyle(cc::PaintFlags::kStroke_Style);
    flags.setStrokeWidth(kDotRingWidth);
    flags.setColor(colors->GetColor(current_ ? kColorMenuCardAccent
                                             : kColorMenuCardMuted));
    canvas->DrawCircle(
        center, kDotSize / 2.0f + kDotRingGap + kDotRingWidth / 2.0f, flags);
  }

  KeyClickAction GetKeyClickActionForEvent(const ui::KeyEvent& event) override {
    return MenuCardKeyClickAction(event);
  }

  void OnFocus() override {
    Button::OnFocus();
    SchedulePaint();
  }

  void OnBlur() override {
    Button::OnBlur();
    SchedulePaint();
  }

 private:
  const tab_groups::TabGroupColorId color_;
  const bool current_;
};

BEGIN_METADATA(ColorDot)
END_METADATA

}  // namespace

// static
std::unique_ptr<views::Widget> TaskCard::Show(
    BrowserWindowInterface* window,
    const tab_groups::TabGroupId& group,
    views::View* row) {
  auto* bubble = new TaskCardBubble(row);
  SetUpMenuCard(bubble);
  bubble->set_highlight_button_when_shown(false);
  bubble->set_adjust_if_offscreen(true);
  bubble->SetContentsView(std::make_unique<TaskCard>(window, group));
  std::unique_ptr<views::Widget> widget =
      views::BubbleDialogDelegate::CreateBubble(bubble);
  ShowInThemeMode(widget.get(), window);
  bubble->Watch(widget.get());
  widget->Show();
  return widget;
}

TaskCard::TaskCard(BrowserWindowInterface* window,
                   const tab_groups::TabGroupId& group)
    : window_(window), group_(group) {
  LayOutMenuCard(this);
  const auto key = [window](int command) -> std::unique_ptr<views::Label> {
    ui::Accelerator accelerator;
    return GetCommandAccelerator(window, command, &accelerator)
               ? MakeMenuCardKey(accelerator)
               : nullptr;
  };
  const auto add = [&](Id id, const gfx::VectorIcon& icon, int text,
                       void (TaskCard::*action)(), int command) {
    auto row = std::make_unique<MenuCardRow>(
        base::BindRepeating(action, base::Unretained(this)), icon,
        l10n_util::GetStringUTF16(text));
    row->SetID(id);
    if (command) {
      if (std::unique_ptr<views::Label> shortcut = key(command)) {
        row->SetEnd(std::move(shortcut));
      }
    }
    return AddChildView(std::move(row));
  };

  add(kRenameId, vector_icons::kEditIcon, IDS_UW_RENAME_TASK_MAC,
      &TaskCard::Rename, kRenameTask);
  const AutomaticOrganizer* organizer = AutomaticOrganizer::From(window);
  if (organizer && organizer->CanAskForName()) {
    add(kNameId, vector_icons::kButtonMagicIcon, IDS_UW_NAME_TASK_MAC,
        &TaskCard::Name, 0)
        ->SetEnabled(!organizer->IsNaming(group.ToString()));
  }
  AddColors();
  AddChildView(MakeMenuCardSeparator());
  add(kNewTabId, kLibraryAddIcon, IDS_TAB_GROUP_HEADER_CXMENU_NEW_TAB_IN_GROUP,
      &TaskCard::NewTab, IDC_ADD_NEW_TAB_TO_GROUP);
  add(kNewWindowId, kMoveGroupIcon,
      IDS_TAB_GROUP_HEADER_CXMENU_MOVE_GROUP_TO_NEW_WINDOW,
      &TaskCard::MoveToNewWindow, 0);
  AddChildView(MakeMenuCardSeparator());
  add(kScratchpadId, kUngroupIcon, IDS_TAB_GROUP_HEADER_CXMENU_UNGROUP,
      &TaskCard::MoveToScratchpad, 0);
  add(kCloseId, kTabCloseIcon, IDS_TAB_GROUP_HEADER_CXMENU_CLOSE_GROUP,
      &TaskCard::CloseTask, IDC_CLOSE_TAB_GROUP);
}

TaskCard::~TaskCard() = default;

void TaskCard::AddColors() {
  const TabGroup* group =
      window_->GetTabStripModel()->group_model()->GetTabGroup(group_);
  const tab_groups::TabGroupColorId current = group->visual_data()->color();
  auto colors = std::make_unique<views::View>();
  colors->SetID(kColorsId);
  colors->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal,
      gfx::Insets::VH(0, kDotsInset)));
  for (const auto& [color, label] : tab_groups::GetTabGroupColorLabelMap()) {
    colors->AddChildView(std::make_unique<ColorDot>(
        base::BindRepeating(&TaskCard::SetColor, base::Unretained(this), color),
        color, color == current));
  }
  AddChildView(std::move(colors));
}

// Each action can close the task, and with it this card, so the action reads
// what it needs first and closes the card only if it is still there.
void TaskCard::Rename() {
  TabOrganizationController* controller =
      TabOrganizationController::From(window_);
  const std::string task = group_.ToString();
  base::WeakPtr<views::Widget> widget = GetWidget()->GetWeakPtr();
  CloseCard(widget);
  if (controller) {
    controller->RenameOnItsRow(task);
  }
}

void TaskCard::Name() {
  AutomaticOrganizer* organizer = AutomaticOrganizer::From(window_);
  const std::string task = group_.ToString();
  base::WeakPtr<views::Widget> widget = GetWidget()->GetWeakPtr();
  CloseCard(widget);
  if (organizer) {
    organizer->AskForName(task);
  }
}

void TaskCard::SetColor(tab_groups::TabGroupColorId color) {
  TabStripModel* tabs = window_->GetTabStripModel();
  const tab_groups::TabGroupId group = group_;
  base::WeakPtr<views::Widget> widget = GetWidget()->GetWeakPtr();
  if (tabs->group_model()->ContainsTabGroup(group)) {
    const tab_groups::TabGroupVisualData* visuals =
        tabs->group_model()->GetTabGroup(group)->visual_data();
    tabs->ChangeTabGroupVisuals(
        group, tab_groups::TabGroupVisualData(visuals->title(), color,
                                              visuals->is_collapsed()));
  }
  CloseCard(widget);
}

void TaskCard::NewTab() {
  TabStripModel* tabs = window_->GetTabStripModel();
  const tab_groups::TabGroupId group = group_;
  base::WeakPtr<views::Widget> widget = GetWidget()->GetWeakPtr();
  if (tabs->group_model()->ContainsTabGroup(group)) {
    const gfx::Range range =
        tabs->group_model()->GetTabGroup(group)->ListTabs();
    tabs->delegate()->AddTabAt(GURL(), range.end(), true, group);
  }
  CloseCard(widget);
}

void TaskCard::MoveToNewWindow() {
  TabStripModel* tabs = window_->GetTabStripModel();
  const tab_groups::TabGroupId group = group_;
  base::WeakPtr<views::Widget> widget = GetWidget()->GetWeakPtr();
  if (tabs->group_model()->ContainsTabGroup(group)) {
    tabs->delegate()->MoveGroupToNewWindow(group);
  }
  CloseCard(widget);
}

// One undoable move of every branch in the task.
void TaskCard::MoveToScratchpad() {
  TabOrganizationController* controller =
      TabOrganizationController::From(window_);
  const std::string task = group_.ToString();
  base::WeakPtr<views::Widget> widget = GetWidget()->GetWeakPtr();
  if (controller) {
    std::vector<PageId> pages;
    for (const Page& page : controller->tree().pages()) {
      if (page.task == task) {
        pages.push_back(page.id);
      }
    }
    controller->MoveToScratchpad(pages);
  }
  CloseCard(widget);
}

// A task that holds every tab leaves a blank tab behind, so the window stays.
void TaskCard::CloseTask() {
  const tab_groups::TabGroupId group = group_;
  base::WeakPtr<views::Widget> widget = GetWidget()->GetWeakPtr();
  if (TabOrganizationController* controller =
          TabOrganizationController::From(window_)) {
    controller->CloseTask(group);
  }
  CloseCard(widget);
}

// static
void TaskCard::CloseCard(base::WeakPtr<views::Widget> widget) {
  if (widget) {
    widget->Close();
  }
}

BEGIN_METADATA(TaskCard)
END_METADATA

}  // namespace uw
