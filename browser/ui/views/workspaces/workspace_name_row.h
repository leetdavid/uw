// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#ifndef UW_BROWSER_UI_VIEWS_WORKSPACES_WORKSPACE_NAME_ROW_H_
#define UW_BROWSER_UI_VIEWS_WORKSPACES_WORKSPACE_NAME_ROW_H_

#include <memory>
#include <string_view>

#include "base/callback_list.h"
#include "base/memory/raw_ptr.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/base/unowned_user_data/scoped_unowned_user_data.h"
#include "ui/views/controls/textfield/textfield_controller.h"
#include "ui/views/view.h"

class BrowserWindowInterface;
class ExpandOnHoverLock;

namespace views {
class Label;
class Textfield;
}  // namespace views

namespace uw {

// The row under the pins that names the workspace the window shows, with its
// emoji. Rename Workspace, or a double-click, turns the name into a field:
// Return and a click elsewhere save it, and Esc puts it back.
class WorkspaceNameRow : public views::View, public views::TextfieldController {
  METADATA_HEADER(WorkspaceNameRow, views::View)

 public:
  DECLARE_USER_DATA(WorkspaceNameRow);

  // Null for a window that shows no workspace, such as a private one.
  static std::unique_ptr<WorkspaceNameRow> Make(BrowserWindowInterface* window);
  static WorkspaceNameRow* From(BrowserWindowInterface* window);

  explicit WorkspaceNameRow(BrowserWindowInterface* window);
  WorkspaceNameRow(const WorkspaceNameRow&) = delete;
  WorkspaceNameRow& operator=(const WorkspaceNameRow&) = delete;
  ~WorkspaceNameRow() override;

  void StartRename();
  bool renaming() const;
  views::Label* name() { return name_; }
  views::Textfield* field() { return field_; }

  // views::View:
  bool OnMousePressed(const ui::MouseEvent& event) override;
  void OnThemeChanged() override;

  // views::TextfieldController:
  bool HandleKeyEvent(views::Textfield* sender,
                      const ui::KeyEvent& key_event) override;

 private:
  void Update();
  void EndRename(bool save);

  const raw_ptr<BrowserWindowInterface> window_;
  raw_ptr<views::Label> icon_ = nullptr;
  raw_ptr<views::Label> name_ = nullptr;
  raw_ptr<views::Textfield> field_ = nullptr;
  bool ending_ = false;
  // Keeps a collapsed sidebar wide while the name is typed.
  std::unique_ptr<ExpandOnHoverLock> expand_lock_;
  base::CallbackListSubscription list_subscription_;
  base::CallbackListSubscription window_subscription_;
  ui::ScopedUnownedUserData<WorkspaceNameRow> user_data_;
};

// Workspace `id`'s emoji and name as the name row shows them, with no
// renaming. The sidebar's slide shows it for a window with no picture yet.
std::unique_ptr<views::View> MakeNameRowLook(std::string_view id);

}  // namespace uw

#endif  // UW_BROWSER_UI_VIEWS_WORKSPACES_WORKSPACE_NAME_ROW_H_
