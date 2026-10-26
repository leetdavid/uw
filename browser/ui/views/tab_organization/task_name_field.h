// Copyright 2026 The uw Authors. All rights reserved.

#ifndef UW_BROWSER_UI_VIEWS_TAB_ORGANIZATION_TASK_NAME_FIELD_H_
#define UW_BROWSER_UI_VIEWS_TAB_ORGANIZATION_TASK_NAME_FIELD_H_

#include <memory>
#include <string>

#include "base/callback_list.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "components/tab_groups/tab_group_id.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/views/controls/textfield/textfield.h"
#include "ui/views/controls/textfield/textfield_controller.h"
#include "ui/views/view_tracker.h"

class ExpandOnHoverLock;

namespace views {
class Label;
}

namespace uw {

class TabOrganizationController;

// Renames a task on its row in the sidebar. While the user types, it takes
// the place of the row's name. Return and a click elsewhere save the name,
// and Esc puts the old one back. See the sidebar menus plan.
class TaskNameField : public views::Textfield,
                      public views::TextfieldController {
  METADATA_HEADER(TaskNameField, views::Textfield)

 public:
  // `name` is the row's name, which the field hides while it shows.
  TaskNameField(const tab_groups::TabGroupId& group, views::Label* name);
  TaskNameField(const TaskNameField&) = delete;
  TaskNameField& operator=(const TaskNameField&) = delete;
  ~TaskNameField() override;

  // The field on the row `row`, or null.
  static TaskNameField* On(views::View* row);

  const tab_groups::TabGroupId& group() const { return group_; }
  bool editing() const { return GetVisible(); }
  // Shows the field with the whole name selected. A name the language model
  // was asked for is dropped: the user's typing wins.
  void Start();

  // views::Textfield:
  void AddedToWidget() override;
  void RemovedFromWidget() override;
  void OnBlur() override;

  // views::TextfieldController:
  bool HandleKeyEvent(views::Textfield* sender,
                      const ui::KeyEvent& key_event) override;

 private:
  // The controller of the window this row is in, looked up each time.
  TabOrganizationController* Controller() const;
  void OnRenameRequested();
  // Puts the row's name back, saving what was typed when `save`. Focus goes
  // back where it was when `restore_focus`.
  void End(bool save, bool restore_focus);

  const tab_groups::TabGroupId group_;
  const raw_ptr<views::Label> name_;
  std::u16string started_with_;
  bool ending_ = false;
  views::ViewTracker previous_focus_;
  // Keeps a hidden sidebar's floating card in while the name is typed.
  std::unique_ptr<ExpandOnHoverLock> expand_lock_;
  base::CallbackListSubscription renames_;
  base::WeakPtrFactory<TaskNameField> weak_factory_{this};
};

}  // namespace uw

#endif  // UW_BROWSER_UI_VIEWS_TAB_ORGANIZATION_TASK_NAME_FIELD_H_
