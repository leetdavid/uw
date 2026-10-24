// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#ifndef UW_BROWSER_UI_VIEWS_TAB_ORGANIZATION_NEW_BUTTON_H_
#define UW_BROWSER_UI_VIEWS_TAB_ORGANIZATION_NEW_BUTTON_H_

#include <memory>

#include "base/memory/raw_ptr.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/menus/simple_menu_model.h"
#include "ui/views/context_menu_controller.h"
#include "ui/views/layout/flex_layout_view.h"

class BrowserWindowInterface;
class ExpandOnHoverLock;

namespace views {
class ImageButton;
class MenuRunner;
}  // namespace views

namespace uw {

// What the new button's caret and a right-click on it list: New tab, New
// task, New chat, New terminal, New split view, and New workspace, each with
// its key.
class NewItemsMenu : public ui::SimpleMenuModel,
                     public ui::SimpleMenuModel::Delegate {
 public:
  explicit NewItemsMenu(BrowserWindowInterface* window);
  NewItemsMenu(const NewItemsMenu&) = delete;
  NewItemsMenu& operator=(const NewItemsMenu&) = delete;
  ~NewItemsMenu() override;

  // ui::SimpleMenuModel::Delegate:
  void ExecuteCommand(int command_id, int event_flags) override;
  bool GetAcceleratorForCommandId(int command_id,
                                  ui::Accelerator* accelerator) const override;

 private:
  const raw_ptr<BrowserWindowInterface> window_;
};

// The split button at the right end of the sidebar's foot. "+" does what
// Cmd+T does, and the caret, or a right-click, opens a NewItemsMenu over the
// page.
class NewButton : public views::FlexLayoutView,
                  public views::ContextMenuController {
  METADATA_HEADER(NewButton, views::FlexLayoutView)

 public:
  explicit NewButton(BrowserWindowInterface* window);
  NewButton(const NewButton&) = delete;
  NewButton& operator=(const NewButton&) = delete;
  ~NewButton() override;

  views::ImageButton* plus() { return plus_; }
  views::ImageButton* caret() { return caret_; }
  bool menu_showing() const;

 private:
  // views::View:
  void OnThemeChanged() override;

  // views::ContextMenuController:
  void ShowContextMenuForViewImpl(
      views::View* source,
      const gfx::Point& point,
      ui::mojom::MenuSourceType source_type) override;

  void ShowMenu();
  void OnMenuClosed();

  const raw_ptr<BrowserWindowInterface> window_;
  raw_ptr<views::ImageButton> plus_ = nullptr;
  raw_ptr<views::ImageButton> caret_ = nullptr;
  std::unique_ptr<NewItemsMenu> menu_model_;
  std::unique_ptr<views::MenuRunner> menu_runner_;
  std::unique_ptr<ExpandOnHoverLock> hover_lock_;
};

}  // namespace uw

#endif  // UW_BROWSER_UI_VIEWS_TAB_ORGANIZATION_NEW_BUTTON_H_
