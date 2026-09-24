// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#ifndef UW_BROWSER_UI_VIEWS_TAB_ORGANIZATION_TAB_TREE_ADAPTER_H_
#define UW_BROWSER_UI_VIEWS_TAB_ORGANIZATION_TAB_TREE_ADAPTER_H_

#include <map>
#include <memory>
#include <optional>
#include <vector>

#include "base/callback_list.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "ui/base/clipboard/clipboard_format_type.h"
#include "ui/base/dragdrop/drop_target_event.h"
#include "ui/compositor/layer_tree_owner.h"
#include "ui/gfx/geometry/point.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/menus/simple_menu_model.h"
#include "ui/views/context_menu_controller.h"
#include "ui/views/drag_controller.h"
#include "ui/views/layout/proposed_layout.h"
#include "ui/views/view.h"
#include "uw/browser/tab_organization/tab_organization_controller.h"

class TabCollectionNode;
namespace views {
class Label;
class LabelButton;
class MenuButton;
class MenuRunner;
}  // namespace views

namespace uw {

// Projects the window's tree into the existing unpinned container. Native
// TabViews stay owned by their Chromium collection nodes; only task headers and
// page disclosure buttons are owned/removed by this adapter.
class TabTreeAdapter : public ui::SimpleMenuModel::Delegate,
                       public views::ContextMenuController,
                       public views::DragController {
 public:
  TabTreeAdapter(views::View* host, TabCollectionNode* collection);
  ~TabTreeAdapter() override;
  bool available() const { return controller_ != nullptr; }
  views::ProposedLayout CalculateLayout(const views::SizeBounds& bounds) const;
  void TrackNativeDrop(const gfx::Point& point_in_screen);
  std::optional<int> LinkDropIndex(const gfx::Point& point) const;

  static ui::ClipboardFormatType DragFormat();
  bool CanDrop(const ui::OSExchangeData& data) const;
  int DragUpdated(const ui::DropTargetEvent& event);
  views::View::DropCallback DropCallback(const ui::DropTargetEvent& event);

 private:
  struct Destination {
    TreeId parent;
    std::optional<TreeId> before;
  };
  struct Row {
    TreeId id;
    gfx::Rect bounds;
  };
  void Refresh();
  void SelectTask(TreeId id);
  void Toggle(TreeId id);
  void OpenMenu(const gfx::Point& point, ui::mojom::MenuSourceType source);
  void EditName(TreeId task);
  std::vector<TreeId> Selection() const;
  std::optional<Destination> DropDestination(const gfx::Point& point) const;
  std::optional<TreeId> ReadDraggedTask(const ui::OSExchangeData& data) const;
  void Drop(TreeId task,
            Destination destination,
            const ui::DropTargetEvent& event,
            ui::mojom::DragOperation& operation,
            std::unique_ptr<ui::LayerTreeOwner> image);

  bool IsCommandIdEnabled(int command) const override;
  void ExecuteCommand(int command, int event_flags) override;
  void ShowContextMenuForViewImpl(
      views::View* source,
      const gfx::Point& point,
      ui::mojom::MenuSourceType source_type) override;
  void WriteDragDataForView(views::View* sender,
                            const gfx::Point& point,
                            ui::OSExchangeData* data) override;
  int GetDragOperationsForView(views::View* sender, const gfx::Point&) override;
  bool CanStartDragForView(views::View* sender,
                           const gfx::Point& press,
                           const gfx::Point& current) override;

  const raw_ptr<views::View> host_;
  const raw_ptr<TabCollectionNode> collection_;
  base::WeakPtr<TabOrganizationController> controller_;
  raw_ptr<views::MenuButton> organize_ = nullptr;
  raw_ptr<views::Label> scratchpad_ = nullptr;
  std::map<TreeId, raw_ptr<views::LabelButton>> tasks_;
  std::map<TreeId, raw_ptr<views::LabelButton>> disclosures_;
  std::optional<TreeId> selected_task_;
  std::vector<TreeId> last_tab_selection_;
  std::vector<TreeId> menu_selection_;
  mutable std::vector<Row> rows_;
  base::CallbackListSubscription subscription_;
  std::unique_ptr<ui::SimpleMenuModel> menu_;
  std::unique_ptr<views::MenuRunner> runner_;
  base::WeakPtrFactory<TabTreeAdapter> weak_factory_{this};
};

}  // namespace uw

#endif  // UW_BROWSER_UI_VIEWS_TAB_ORGANIZATION_TAB_TREE_ADAPTER_H_
