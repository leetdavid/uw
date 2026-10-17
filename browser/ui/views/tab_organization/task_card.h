// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#ifndef UW_BROWSER_UI_VIEWS_TAB_ORGANIZATION_TASK_CARD_H_
#define UW_BROWSER_UI_VIEWS_TAB_ORGANIZATION_TASK_CARD_H_

#include <memory>

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "components/tab_groups/tab_group_color.h"
#include "components/tab_groups/tab_group_id.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/views/view.h"

class BrowserWindowInterface;

namespace views {
class Widget;
}

namespace uw {

// The task menu: a card under a task's row in the sidebar that renames and
// names the task, sets its color, and moves or closes its tabs. It takes the
// place of Chromium's task editor there. See the sidebar menus plan.
class TaskCard : public views::View {
  METADATA_HEADER(TaskCard, views::View)

 public:
  // Views tests reach by id.
  enum Id {
    kRenameId = 1,
    kNameId,
    kColorsId,
    kNewTabId,
    kNewWindowId,
    kScratchpadId,
    kCloseId,
  };

  // Shows the card of `group` under `row`, the task's row. The caller keeps
  // the widget; the card's bubble deletes itself once the widget is gone.
  static std::unique_ptr<views::Widget> Show(
      BrowserWindowInterface* window,
      const tab_groups::TabGroupId& group,
      views::View* row);

  TaskCard(BrowserWindowInterface* window, const tab_groups::TabGroupId& group);
  TaskCard(const TaskCard&) = delete;
  TaskCard& operator=(const TaskCard&) = delete;
  ~TaskCard() override;

 private:
  void AddColors();
  void Rename();
  void Name();
  void SetColor(tab_groups::TabGroupColorId color);
  void NewTab();
  void MoveToNewWindow();
  void MoveToScratchpad();
  void CloseTask();
  static void CloseCard(base::WeakPtr<views::Widget> widget);

  const raw_ptr<BrowserWindowInterface> window_;
  const tab_groups::TabGroupId group_;
};

}  // namespace uw

#endif  // UW_BROWSER_UI_VIEWS_TAB_ORGANIZATION_TASK_CARD_H_
