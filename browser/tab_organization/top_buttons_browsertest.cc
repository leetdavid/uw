// Copyright 2026 The uw Authors. All rights reserved.

#include <optional>
#include <string>
#include <vector>

#include "base/functional/function_ref.h"
#include "base/test/run_until.h"
#include "chrome/browser/headless/test/headless_mode_browsertest.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_element_identifiers.h"
#include "chrome/browser/ui/tabs/tab_group_model.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/tabs/shared/tab_strip_flat_edge_button.h"
#include "chrome/browser/ui/views/tabs/vertical/top_container_button.h"
#include "components/tab_groups/tab_group_id.h"
#include "components/tab_groups/tab_group_visual_data.h"
#include "components/tabs/public/tab_group.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/test/browser_test.h"
#include "ui/base/page_transition_types.h"
#include "ui/events/base_event_utils.h"
#include "ui/events/event.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/views/controls/button/menu_button_controller.h"
#include "ui/views/controls/label.h"
#include "ui/views/test/button_test_api.h"
#include "ui/views/view_class_properties.h"
#include "ui/views/view_utils.h"
#include "ui/views/widget/widget.h"
#include "url/gurl.h"
#include "uw/browser/tab_organization/tab_organization_controller.h"
#include "uw/browser/tab_organization/window_screenshot.h"
#include "uw/browser/ui/look/top_button.h"
#include "uw/browser/ui/views/tab_organization/inbox_button.h"
#include "uw/browser/ui/views/tab_organization/menu_card_row.h"
#include "uw/browser/ui/views/tab_organization/tasks_card.h"

namespace uw {
namespace {

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

// The buttons at the top of the sidebar, and the list Tasks opens. See the
// Doop guide uw-hidden-sidebar, "Top buttons".
class TopButtonsBrowserTest : public headless::HeadlessModeBrowserTest {
 protected:
  TabOrganizationController* controller() {
    return TabOrganizationController::From(browser());
  }

  TabStripModel* tabs() { return browser()->tab_strip_model(); }

  static views::View* RootOf(Browser* window) {
    return BrowserView::GetBrowserViewForBrowser(window)
        ->GetWidget()
        ->GetRootView();
  }

  static TabStripFlatEdgeButton* ButtonOf(Browser* window,
                                          ui::ElementIdentifier id) {
    return FindIn<TabStripFlatEdgeButton>(
        RootOf(window), [id](TabStripFlatEdgeButton* button) {
          return button->GetProperty(views::kElementIdentifierKey) == id &&
                 button->IsDrawn();
        });
  }

  static TabStripFlatEdgeButton* TasksOf(Browser* window) {
    return ButtonOf(window, kSavedTabGroupButtonElementId);
  }

  static TopContainerButton* ToggleOf(Browser* window) {
    return FindIn<TopContainerButton>(
        RootOf(window),
        [](TopContainerButton* button) { return button->IsDrawn(); });
  }

  static InboxButton* InboxOf(Browser* window) {
    return FindIn<InboxButton>(RootOf(window),
                               [](InboxButton*) { return true; });
  }

  tabs::TabInterface* AddTab() {
    EXPECT_TRUE(AddTabAtIndex(tabs()->count(), GURL("about:blank"),
                              ui::PAGE_TRANSITION_TYPED));
    return tabs()->GetActiveTab();
  }

  tab_groups::TabGroupId TaskOf(std::vector<tabs::TabInterface*> members,
                                const std::u16string& title) {
    std::vector<PageId> pages;
    for (tabs::TabInterface* tab : members) {
      pages.push_back(controller()->IdFor(tab).value());
    }
    const std::optional<tab_groups::TabGroupId> task =
        controller()->MoveToNewTask(pages, title);
    CHECK(task);
    return *task;
  }

  void Select(tabs::TabInterface* tab) {
    tabs()->ActivateTabAt(tabs()->GetIndexOfTab(tab));
  }

  views::Widget* TasksCardWidget() {
    for (views::Widget* widget : views::Widget::GetAllOwnedWidgets(
             BrowserView::GetBrowserViewForBrowser(browser())
                 ->GetWidget()
                 ->GetNativeView())) {
      if (!widget->IsClosed() && widget->widget_delegate() &&
          views::IsViewClass<TasksCard>(
              widget->widget_delegate()->GetContentsView())) {
        return widget;
      }
    }
    return nullptr;
  }

  // Presses Tasks, as a click does, and returns its card once it shows.
  views::Widget* OpenTasks() {
    static_cast<views::MenuButtonController*>(
        TasksOf(browser())->button_controller())
        ->Activate(nullptr);
    views::Widget* card = nullptr;
    EXPECT_TRUE(base::test::RunUntil([&] {
      card = TasksCardWidget();
      return card && card->IsVisible();
    }));
    return card;
  }

  static views::View* RowOf(views::Widget* card, int id) {
    return card->widget_delegate()->GetContentsView()->GetViewByID(id);
  }

  static std::vector<std::u16string> TaskTitles(views::Widget* card) {
    std::vector<std::u16string> titles;
    for (int id = TasksCard::kFirstTaskId;
         auto* row = views::AsViewClass<MenuCardRow>(RowOf(card, id)); ++id) {
      titles.emplace_back(row->label()->GetText());
    }
    return titles;
  }

  void Click(views::View* row) {
    views::test::ButtonTestApi(views::AsViewClass<views::Button>(row))
        .NotifyClick(ui::MouseEvent(ui::EventType::kMousePressed, gfx::Point(),
                                    gfx::Point(), ui::EventTimeForNow(),
                                    ui::EF_LEFT_MOUSE_BUTTON, 0));
  }
};

IN_PROC_BROWSER_TEST_F(TopButtonsBrowserTest, TheButtonsShareOneLine) {
  views::View* toggle = ToggleOf(browser());
  views::View* tasks = TasksOf(browser());
  views::View* search = ButtonOf(browser(), kTabSearchButtonElementId);
  views::View* inbox = InboxOf(browser());
  ASSERT_TRUE(toggle && tasks && search && inbox);
  const gfx::Rect line[] = {
      toggle->GetBoundsInScreen(), tasks->GetBoundsInScreen(),
      search->GetBoundsInScreen(), inbox->GetBoundsInScreen()};
  for (const gfx::Rect& button : line) {
    EXPECT_EQ(button.size(), gfx::Size(kTopButtonSize, kTopButtonSize));
    EXPECT_EQ(button.CenterPoint().y(), line[0].CenterPoint().y());
  }
  EXPECT_EQ(line[2].x() - line[1].right(), kTopButtonSpacing);
  EXPECT_EQ(line[3].x() - line[2].right(), kTopButtonSpacing);
  EXPECT_EQ(tasks->GetRenderedTooltipText(gfx::Point()), u"Tasks");
  SaveWindowScreenshot(browser(), "top-buttons");

  for (views::View* button : {toggle, tasks, search}) {
    views::AsViewClass<views::Button>(button)->SetState(
        views::Button::STATE_HOVERED);
  }
  SaveWindowScreenshot(browser(), "top-buttons-lit");
}

IN_PROC_BROWSER_TEST_F(TopButtonsBrowserTest,
                       TasksListsTheOpenTasksInTheSidebarsOrder) {
  tabs::TabInterface* flights = tabs()->GetActiveTab();
  tabs::TabInterface* desk = AddTab();
  tabs::TabInterface* passport = AddTab();
  AddTab();
  TaskOf({flights}, u"Plan Lisbon trip");
  TaskOf({desk}, u"Pick a standing desk");
  const tab_groups::TabGroupId closed = TaskOf({passport}, u"Renew passport");
  controller()->CloseTask(closed);
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return !tabs()->group_model()->ContainsTabGroup(closed); }));

  views::Widget* card = OpenTasks();
  ASSERT_TRUE(card);
  EXPECT_TRUE(RowOf(card, TasksCard::kNewTaskId));
  EXPECT_EQ(TaskTitles(card),
            (std::vector<std::u16string>{u"Plan Lisbon trip",
                                         u"Pick a standing desk"}));
  EXPECT_EQ(TasksOf(browser())->GetState(), views::Button::STATE_PRESSED);
  SaveWindowScreenshot(browser(), "top-buttons-tasks-open");
  SaveWidgetScreenshot(card, "tasks-card");

  card->CloseNow();
  EXPECT_NE(TasksOf(browser())->GetState(), views::Button::STATE_PRESSED);
}

IN_PROC_BROWSER_TEST_F(TopButtonsBrowserTest,
                       PickingATaskSelectsTheTabUsedLastInIt) {
  tabs::TabInterface* flights = tabs()->GetActiveTab();
  tabs::TabInterface* hotels = AddTab();
  tabs::TabInterface* mail = AddTab();
  TaskOf({flights, hotels}, u"Plan Lisbon trip");
  Select(hotels);
  Select(mail);

  views::Widget* card = OpenTasks();
  ASSERT_TRUE(card);
  Click(RowOf(card, TasksCard::kFirstTaskId));
  EXPECT_TRUE(base::test::RunUntil([&] { return !TasksCardWidget(); }));
  EXPECT_EQ(tabs()->GetActiveTab(), hotels);
}

IN_PROC_BROWSER_TEST_F(TopButtonsBrowserTest, PickingACollapsedTaskOpensIt) {
  tabs::TabInterface* flights = tabs()->GetActiveTab();
  AddTab();
  const tab_groups::TabGroupId task = TaskOf({flights}, u"Plan Lisbon trip");
  tabs()->ChangeTabGroupVisuals(
      task, tab_groups::TabGroupVisualData(u"Plan Lisbon trip",
                                           tab_groups::TabGroupColorId::kBlue,
                                           /*is_collapsed=*/true));
  ASSERT_TRUE(tabs()->IsGroupCollapsed(task));

  views::Widget* card = OpenTasks();
  ASSERT_TRUE(card);
  Click(RowOf(card, TasksCard::kFirstTaskId));
  EXPECT_TRUE(base::test::RunUntil([&] {
    return tabs()->GetActiveTab() == flights && !tabs()->IsGroupCollapsed(task);
  }));
}

IN_PROC_BROWSER_TEST_F(TopButtonsBrowserTest, NewTaskStartsATask) {
  views::Widget* card = OpenTasks();
  ASSERT_TRUE(card);
  EXPECT_TRUE(TaskTitles(card).empty());
  Click(RowOf(card, TasksCard::kNewTaskId));
  EXPECT_TRUE(base::test::RunUntil([&] {
    return tabs()->group_model()->ListTabGroups().size() == 1u &&
           tabs()->GetActiveTab()->GetGroup().has_value();
  }));
}

// Move to New Task sits under New Task and moves the selected tab and the tabs
// under it to a new task.
IN_PROC_BROWSER_TEST_F(TopButtonsBrowserTest,
                       MoveToNewTaskMovesTheSelectedTab) {
  tabs::TabInterface* flights = tabs()->GetActiveTab();
  AddTab();
  Select(flights);

  views::Widget* card = OpenTasks();
  ASSERT_TRUE(card);
  Click(RowOf(card, TasksCard::kMoveToNewTaskId));
  EXPECT_TRUE(base::test::RunUntil([&] {
    return tabs()->group_model()->ListTabGroups().size() == 1u &&
           flights->GetGroup().has_value();
  }));
}

IN_PROC_BROWSER_TEST_F(TopButtonsBrowserTest, APrivateWindowHasTasks) {
  Browser* private_window = CreateIncognitoBrowser();
  EXPECT_TRUE(TasksOf(private_window));
  EXPECT_FALSE(InboxOf(private_window));
}

}  // namespace
}  // namespace uw
