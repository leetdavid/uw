// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#ifndef UW_BROWSER_UI_VIEWS_WORKSPACES_WORKSPACE_SWITCHER_H_
#define UW_BROWSER_UI_VIEWS_WORKSPACES_WORKSPACE_SWITCHER_H_

#include <memory>
#include <string>
#include <string_view>

#include "base/callback_list.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/base/unowned_user_data/scoped_unowned_user_data.h"
#include "ui/views/context_menu_controller.h"
#include "ui/views/view.h"

class BrowserWindowInterface;
class ExpandOnHoverLock;

namespace views {
class Button;
class Widget;
}  // namespace views

namespace uw {

class CardBubble;
class WorkspaceSwipe;

// The workspaces in the middle of the sidebar's foot: an icon for each, the
// one this window shows on the tint's fill. A click switches to it and a
// right-click opens its menu. From five workspaces on, each one but the
// current is a dot. It also opens the workspace cards, one at a time.
class WorkspaceSwitcher : public views::View,
                          public views::ContextMenuController {
  METADATA_HEADER(WorkspaceSwitcher, views::View)

 public:
  DECLARE_USER_DATA(WorkspaceSwitcher);

  // Null for a window that shows no workspace, such as a private one.
  static std::unique_ptr<WorkspaceSwitcher> Make(
      BrowserWindowInterface* window);
  static WorkspaceSwitcher* From(BrowserWindowInterface* window);

  explicit WorkspaceSwitcher(BrowserWindowInterface* window);
  WorkspaceSwitcher(const WorkspaceSwitcher&) = delete;
  WorkspaceSwitcher& operator=(const WorkspaceSwitcher&) = delete;
  ~WorkspaceSwitcher() override;

  // Opens the new workspace card above the sidebar's foot.
  void ShowNewWorkspaceCard();
  // Opens the menu of workspace `id` above its icon.
  void ShowMenu(const std::string& id);
  // Opens the icon picker of workspace `id` above its icon.
  void ShowIconCard(const std::string& id);
  // Asks before deleting workspace `id`, over the page.
  void ShowDeleteCard(const std::string& id);

  // While the sidebar slides toward workspace `to`, the current icon's fill
  // glides toward its icon by `amount`, from 0 to 1. An empty `to` puts it
  // back.
  void SetSlide(const std::string& to, float amount);

  views::Button* IconFor(std::string_view id);
  // The card or menu that is open, if any.
  views::Widget* card() { return card_.get(); }
  BrowserWindowInterface* window() { return window_; }
  base::WeakPtr<WorkspaceSwitcher> GetWeakPtr() {
    return weak_factory_.GetWeakPtr();
  }

  // views::View:
  void AddedToWidget() override;
  void OnPaint(gfx::Canvas* canvas) override;

  // views::ContextMenuController:
  void ShowContextMenuForViewImpl(
      views::View* source,
      const gfx::Point& point,
      ui::mojom::MenuSourceType source_type) override;

 private:
  void Rebuild();
  // Shows `bubble` as the open card, closing any other. `ringed` is the
  // workspace whose icon keeps a ring while it is open.
  void ShowCard(CardBubble* bubble, const std::string& ringed);
  void OnCardClosing();
  base::OnceClosure CardClosing();

  const raw_ptr<BrowserWindowInterface> window_;
  std::string ringed_;
  std::unique_ptr<views::Widget> card_;
  std::unique_ptr<ExpandOnHoverLock> hover_lock_;
  std::unique_ptr<WorkspaceSwipe> swipe_;
  std::string slide_to_;
  float slide_amount_ = 0;
  base::CallbackListSubscription list_subscription_;
  base::CallbackListSubscription window_subscription_;
  ui::ScopedUnownedUserData<WorkspaceSwitcher> user_data_;
  base::WeakPtrFactory<WorkspaceSwitcher> weak_factory_{this};
};

}  // namespace uw

#endif  // UW_BROWSER_UI_VIEWS_WORKSPACES_WORKSPACE_SWITCHER_H_
