// Copyright 2026 The uw Authors. All rights reserved.

#ifndef UW_BROWSER_UI_VIEWS_TAB_ORGANIZATION_TASKS_CARD_H_
#define UW_BROWSER_UI_VIEWS_TAB_ORGANIZATION_TASKS_CARD_H_

#include <memory>
#include <string>

#include "base/memory/raw_ptr.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/views/view.h"

class BrowserWindowInterface;

namespace views {
class MenuButtonController;
class Widget;
}  // namespace views

namespace uw {

// The Tasks list: a card under Tasks, at the top of the sidebar, with New Task
// and the workspace's open tasks. It takes the place of Chromium's saved tab
// groups menu. See docs/product.md, "Automatic tab organization".
class TasksCard : public views::View {
  METADATA_HEADER(TasksCard, views::View)

 public:
  // Views tests reach by id. The task rows take kFirstTaskId and on, in the
  // sidebar's order.
  enum Id {
    kNewTaskId = 1,
    kMoveToNewTaskId,
    kFirstTaskId,
  };

  // Shows the card under `button`, which `controller` holds pressed while the
  // card is open. The caller keeps the widget.
  static std::unique_ptr<views::Widget> Show(
      BrowserWindowInterface* window,
      views::View* button,
      views::MenuButtonController* controller);

  explicit TasksCard(BrowserWindowInterface* window);
  TasksCard(const TasksCard&) = delete;
  TasksCard& operator=(const TasksCard&) = delete;
  ~TasksCard() override;

 private:
  void NewTask();
  void MoveToNewTask();
  void GoToTask(const std::string& task);

  const raw_ptr<BrowserWindowInterface> window_;
};

}  // namespace uw

#endif  // UW_BROWSER_UI_VIEWS_TAB_ORGANIZATION_TASKS_CARD_H_
