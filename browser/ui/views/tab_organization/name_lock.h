// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#ifndef UW_BROWSER_UI_VIEWS_TAB_ORGANIZATION_NAME_LOCK_H_
#define UW_BROWSER_UI_VIEWS_TAB_ORGANIZATION_NAME_LOCK_H_

#include "base/callback_list.h"
#include "components/prefs/pref_change_registrar.h"
#include "components/tab_groups/tab_group_id.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/views/controls/button/image_button.h"

namespace uw {

class TabOrganizationController;

// A small lock on a task's row while the task has a name the user typed,
// which uw never replaces. A click hands the name back to uw. It is hidden
// on every other task, and while automatic organization is off: nothing
// renames a task then.
class NameLock : public views::ImageButton {
  METADATA_HEADER(NameLock, views::ImageButton)

 public:
  explicit NameLock(const tab_groups::TabGroupId& group);
  NameLock(const NameLock&) = delete;
  NameLock& operator=(const NameLock&) = delete;
  ~NameLock() override;

  const tab_groups::TabGroupId& group() const { return group_; }

  // views::ImageButton:
  void AddedToWidget() override;
  void RemovedFromWidget() override;
  void OnThemeChanged() override;

 private:
  // The controller of the window this row is in. Looked up each time: the
  // window owns it, not this view.
  TabOrganizationController* Controller() const;
  void Refresh();
  void HandBack();

  const tab_groups::TabGroupId group_;
  base::CallbackListSubscription changed_;
  PrefChangeRegistrar prefs_;
};

}  // namespace uw

#endif  // UW_BROWSER_UI_VIEWS_TAB_ORGANIZATION_NAME_LOCK_H_
