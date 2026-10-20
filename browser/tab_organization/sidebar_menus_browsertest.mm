// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#import <AppKit/AppKit.h>

#include <optional>
#include <string>

#include "base/functional/function_ref.h"
#include "base/run_loop.h"
#include "base/task/single_thread_task_runner.h"
#include "base/time/time.h"
#include "chrome/browser/headless/test/headless_mode_browsertest.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_group_model.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/tabs/common/tab_group_header_view.h"
#include "components/tab_groups/tab_group_id.h"
#include "components/tabs/public/tab_group.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/test/browser_test.h"
#include "ui/events/keycodes/keyboard_codes.h"
#import "ui/events/test/cocoa_test_event_utils.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/mac/coordinate_conversion.h"
#include "ui/views/controls/button/md_text_button.h"
#include "ui/views/focus/focus_manager.h"
#include "ui/views/view.h"
#include "ui/views/view_utils.h"
#include "ui/views/widget/widget.h"
#include "uw/browser/tab_organization/commands.h"
#include "uw/browser/tab_organization/tab_organization_controller.h"
#include "uw/browser/tab_organization/window_screenshot.h"
#include "uw/browser/ui/views/tab_organization/organize_bar.h"
#include "uw/browser/ui/views/tab_organization/organize_card.h"
#include "uw/browser/ui/views/tab_organization/task_card.h"
#include "uw/browser/ui/views/tab_organization/task_name_field.h"

namespace uw {
namespace {

constexpr NSEventModifierFlags kCtrlCmd =
    NSEventModifierFlagControl | NSEventModifierFlagCommand;

template <typename Found>
Found* FindIn(views::View* view, base::FunctionRef<bool(Found*)> matches) {
  if (Found* found = views::AsViewClass<Found>(view); found && matches(found)) {
    return found;
  }
  for (views::View* child : view->children()) {
    if (Found* found = FindIn<Found>(child, matches)) {
      return found;
    }
  }
  return nullptr;
}

// The sidebar's cards and name field take real clicks and keys. A headless
// window never takes keyboard focus, so these need a real one. Run them with
// `pnpm run window-tests`.
class SidebarMenusBrowserTest : public headless::HeadlessModeBrowserTest {
 protected:
  TabOrganizationController* controller() {
    return TabOrganizationController::From(browser());
  }

  BrowserView* view() {
    return BrowserView::GetBrowserViewForBrowser(browser());
  }

  TabStripModel* tabs() { return browser()->tab_strip_model(); }

  // A task named "Plan Lisbon trip" that holds the first tab, once its row
  // has stopped moving, so a click lands on it.
  tab_groups::TabGroupId StageTask() {
    const std::optional<tab_groups::TabGroupId> task =
        controller()->MoveToNewTask(
            {controller()->IdFor(tabs()->GetTabAtIndex(0)).value()},
            u"Plan Lisbon trip");
    CHECK(task);
    gfx::Rect last;
    CHECK(Eventually([&] {
      TabGroupHeaderView* row = RowOf(*task);
      if (!row || !row->IsDrawn()) {
        return false;
      }
      const gfx::Rect now = row->GetBoundsInScreen();
      const bool still = !now.IsEmpty() && now == last;
      last = now;
      Wait(base::Milliseconds(100));
      return still;
    }));
    return *task;
  }

  TaskNameField* FieldOf(const tab_groups::TabGroupId& task) {
    return FindIn<TaskNameField>(
        view(), [&](TaskNameField* field) { return field->group() == task; });
  }

  TabGroupHeaderView* RowOf(const tab_groups::TabGroupId& task) {
    TaskNameField* field = FieldOf(task);
    return field ? views::AsViewClass<TabGroupHeaderView>(field->parent())
                 : nullptr;
  }

  const tab_groups::TabGroupVisualData& Visuals(
      const tab_groups::TabGroupId& task) {
    return *tabs()->group_model()->GetTabGroup(task)->visual_data();
  }

  // The open task card, or null.
  views::Widget* TaskCardWidget() {
    for (views::Widget* widget : views::Widget::GetAllOwnedWidgets(
             view()->GetWidget()->GetNativeView())) {
      if (!widget->IsClosed() && widget->widget_delegate() &&
          views::IsViewClass<TaskCard>(
              widget->widget_delegate()->GetContentsView())) {
        return widget;
      }
    }
    return nullptr;
  }

  // Waits for `card` to take the keyboard, with Full Keyboard Access off as
  // macOS ships it: the arrow keys must still reach every row.
  static bool TakesTheKeyboard(views::Widget* card) {
    if (!Eventually([&] { return card->IsActive(); })) {
      return false;
    }
    card->GetFocusManager()->SetKeyboardAccessible(false);
    return true;
  }

  static int FocusedId(views::Widget* widget) {
    views::View* focused = widget->GetFocusManager()->GetFocusedView();
    return focused ? focused->GetID() : 0;
  }

  // Clicks the middle of `target` with real mouse events, `count` times in a
  // row as a double-click does.
  static void Click(views::View* target,
                    NSEventType down,
                    NSEventType up,
                    int count = 1) {
    NSWindow* window =
        target->GetWidget()->GetNativeWindow().GetNativeNSWindow();
    const NSPoint point = [window
        convertPointFromScreen:gfx::ScreenPointToNSPoint(
                                   target->GetBoundsInScreen().CenterPoint())];
    for (int click = 1; click <= count; ++click) {
      [window sendEvent:cocoa_test_event_utils::MouseEventAtPointInWindow(
                            point, down, window, click)];
      [window sendEvent:cocoa_test_event_utils::MouseEventAtPointInWindow(
                            point, up, window, click)];
    }
  }

  // Types a key into the window that has keyboard focus, as a person does.
  static void Key(ui::KeyboardCode key, NSEventModifierFlags modifiers = 0) {
    NSWindow* window = NSApp.keyWindow;
    ASSERT_TRUE(window);
    for (bool down : {true, false}) {
      [NSApp postEvent:cocoa_test_event_utils::SynthesizeKeyEvent(
                           window, down, key, modifiers)
               atStart:NO];
    }
    Wait(base::Milliseconds(60));
  }

  static void Type(std::string_view text) {
    for (char letter : text) {
      Key(letter == ' '
              ? ui::VKEY_SPACE
              : static_cast<ui::KeyboardCode>(ui::VKEY_A + letter - 'a'));
    }
  }

  static void Wait(base::TimeDelta delay) {
    base::RunLoop wait;
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
        FROM_HERE, wait.QuitClosure(), delay);
    wait.Run();
  }

  template <typename Condition>
  static bool Eventually(Condition condition,
                         base::TimeDelta within = base::Seconds(3)) {
    const base::TimeTicks deadline = base::TimeTicks::Now() + within;
    while (base::TimeTicks::Now() < deadline) {
      if (condition()) {
        return true;
      }
      Wait(base::Milliseconds(20));
    }
    return condition();
  }
};

// A right-click opens the card under the row, where it takes the keyboard:
// the arrow keys move between rows, and Esc closes it.
IN_PROC_BROWSER_TEST_F(SidebarMenusBrowserTest,
                       MANUAL_TheTaskCardOpensUnderTheRowAndTakesTheArrowKeys) {
  const tab_groups::TabGroupId task = StageTask();
  TabGroupHeaderView* row = RowOf(task);
  ASSERT_TRUE(row);

  Click(row, NSEventTypeRightMouseDown, NSEventTypeRightMouseUp);
  ASSERT_TRUE(Eventually([&] { return TaskCardWidget(); }));
  views::Widget* card = TaskCardWidget();
  ASSERT_TRUE(TakesTheKeyboard(card));
  const gfx::Rect row_bounds = row->GetBoundsInScreen();
  const gfx::Rect card_bounds =
      card->widget_delegate()->GetContentsView()->GetBoundsInScreen();
  EXPECT_GE(card_bounds.y(), row_bounds.bottom() - 2)
      << card_bounds.ToString() << " under " << row_bounds.ToString();
  EXPECT_LE(card_bounds.y(), row_bounds.bottom() + 24)
      << card_bounds.ToString() << " under " << row_bounds.ToString();
  EXPECT_NEAR(card_bounds.x(), row_bounds.x(), 16)
      << card_bounds.ToString() << " under " << row_bounds.ToString();

  const int first = FocusedId(card);
  Key(ui::VKEY_DOWN);
  const int second = FocusedId(card);
  EXPECT_NE(second, 0) << "Down left no row focused";
  EXPECT_NE(second, first) << "Down did not move";
  Key(ui::VKEY_DOWN);
  EXPECT_NE(FocusedId(card), second) << "a second Down did not move";
  Key(ui::VKEY_UP);
  EXPECT_EQ(FocusedId(card), second) << "Up did not move back";

  Key(ui::VKEY_ESCAPE);
  EXPECT_TRUE(Eventually([&] { return !TaskCardWidget(); }));
  EXPECT_EQ(Visuals(task).title(), u"Plan Lisbon trip");
}

// Return on Rename Task closes the card and leaves the name field with the
// keyboard, and what is typed there is the task's new name.
IN_PROC_BROWSER_TEST_F(SidebarMenusBrowserTest,
                       MANUAL_RenameTaskOnTheCardTakesTypedText) {
  const tab_groups::TabGroupId task = StageTask();
  TabGroupHeaderView* row = RowOf(task);
  ASSERT_TRUE(row);
  TaskNameField* field = FieldOf(task);

  Click(row, NSEventTypeRightMouseDown, NSEventTypeRightMouseUp);
  ASSERT_TRUE(Eventually([&] { return TaskCardWidget(); }));
  views::Widget* card = TaskCardWidget();
  ASSERT_TRUE(TakesTheKeyboard(card));
  for (int key = 0; key < 12 && FocusedId(card) != TaskCard::kRenameId; ++key) {
    Key(ui::VKEY_DOWN);
  }
  ASSERT_EQ(FocusedId(card), TaskCard::kRenameId);
  Key(ui::VKEY_RETURN);

  ASSERT_TRUE(Eventually([&] { return !TaskCardWidget(); }));
  ASSERT_TRUE(Eventually([&] { return field->editing() && field->HasFocus(); }))
      << "editing " << field->editing() << ", focused " << field->HasFocus();
  Type("porto trip");
  EXPECT_EQ(field->GetText(), u"porto trip");
  Key(ui::VKEY_RETURN);
  EXPECT_FALSE(field->editing());
  EXPECT_EQ(Visuals(task).title(), u"porto trip");
}

// A real double-click on the name makes it a field that takes typed text.
// The task stays open, and Esc puts the old name back.
IN_PROC_BROWSER_TEST_F(SidebarMenusBrowserTest,
                       MANUAL_ADoubleClickOnTheNameTakesTypedText) {
  const tab_groups::TabGroupId task = StageTask();
  TabGroupHeaderView* row = RowOf(task);
  ASSERT_TRUE(row);
  TaskNameField* field = FieldOf(task);
  ASSERT_FALSE(Visuals(task).is_collapsed());

  Click(row, NSEventTypeLeftMouseDown, NSEventTypeLeftMouseUp, 2);
  ASSERT_TRUE(Eventually([&] { return field->editing() && field->HasFocus(); }))
      << "editing " << field->editing() << ", focused " << field->HasFocus();
  EXPECT_FALSE(Visuals(task).is_collapsed());
  SaveWindowScreenshot(browser(), "rename-in-place-window");
  Type("lisbon in may");
  EXPECT_EQ(field->GetText(), u"lisbon in may");
  Key(ui::VKEY_ESCAPE);
  EXPECT_FALSE(field->editing());
  EXPECT_EQ(Visuals(task).title(), u"Plan Lisbon trip");

  Click(row, NSEventTypeLeftMouseDown, NSEventTypeLeftMouseUp, 2);
  ASSERT_TRUE(
      Eventually([&] { return field->editing() && field->HasFocus(); }));
  Type("lisbon in may");
  Key(ui::VKEY_RETURN);
  EXPECT_EQ(Visuals(task).title(), u"lisbon in may");
  EXPECT_FALSE(Visuals(task).is_collapsed());
}

// Ctrl+Cmd+N opens a new task whose name field keeps the keyboard while its
// new tab loads.
IN_PROC_BROWSER_TEST_F(SidebarMenusBrowserTest,
                       MANUAL_ANewTaskTakesItsNameAsTyped) {
  StageTask();
  const int count = tabs()->count();
  Key(ui::VKEY_N, kCtrlCmd);
  ASSERT_TRUE(Eventually([&] { return tabs()->count() == count + 1; }));
  const std::optional<tab_groups::TabGroupId> task =
      tabs()->GetActiveTab()->GetGroup();
  ASSERT_TRUE(task);
  ASSERT_TRUE(Eventually([&] {
    TaskNameField* field = FieldOf(*task);
    return field && field->editing() && field->HasFocus();
  }));
  TaskNameField* field = FieldOf(*task);
  Wait(base::Milliseconds(500));
  ASSERT_TRUE(field->editing() && field->HasFocus())
      << "the name field lost the keyboard as the task's row grew in";
  Type("hotels");
  Key(ui::VKEY_RETURN);
  EXPECT_EQ(Visuals(*task).title(), u"hotels");
}

// The Organize card opens over its button and takes the arrow keys.
IN_PROC_BROWSER_TEST_F(SidebarMenusBrowserTest,
                       MANUAL_TheOrganizeCardOpensOverItsButton) {
  StageTask();
  OrganizeBar* bar =
      FindIn<OrganizeBar>(view(), [](OrganizeBar*) { return true; });
  ASSERT_TRUE(bar);
  views::MdTextButton* pill =
      FindIn<views::MdTextButton>(bar, [](views::MdTextButton* button) {
        return button->GetText() == u"Organize";
      });
  ASSERT_TRUE(pill);

  Click(pill, NSEventTypeLeftMouseDown, NSEventTypeLeftMouseUp);
  ASSERT_TRUE(Eventually([&] { return bar->organize_card(); }));
  views::Widget* card = bar->organize_card();
  ASSERT_TRUE(TakesTheKeyboard(card));
  const gfx::Rect pill_bounds = pill->GetBoundsInScreen();
  const gfx::Rect card_bounds =
      card->widget_delegate()->GetContentsView()->GetBoundsInScreen();
  EXPECT_LE(card_bounds.bottom(), pill_bounds.y() + 2)
      << card_bounds.ToString() << " over " << pill_bounds.ToString();
  EXPECT_GE(card_bounds.bottom(), pill_bounds.y() - 24)
      << card_bounds.ToString() << " over " << pill_bounds.ToString();
  EXPECT_NEAR(card_bounds.x(), pill_bounds.x(), 16)
      << card_bounds.ToString() << " over " << pill_bounds.ToString();

  const int first = FocusedId(card);
  Key(ui::VKEY_DOWN);
  EXPECT_NE(FocusedId(card), 0) << "Down left no row focused";
  EXPECT_NE(FocusedId(card), first) << "Down did not move";
  Key(ui::VKEY_ESCAPE);
  EXPECT_TRUE(Eventually([&] { return !bar->organize_card(); }));
}

}  // namespace
}  // namespace uw
