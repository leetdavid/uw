// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/browser/ui/views/tab_organization/tab_tree_adapter.h"

#include <algorithm>
#include <functional>
#include <set>
#include <variant>

#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/pickle.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/tabs/common/tab_collection_node.h"
#include "chrome/grit/generated_resources.h"
#include "components/tabs/public/tab_collection_types.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/base/dragdrop/drag_drop_types.h"
#include "ui/base/dragdrop/drop_target_event.h"
#include "ui/base/dragdrop/mojom/drag_drop_types.mojom.h"
#include "ui/base/dragdrop/os_exchange_data.h"
#include "ui/base/interaction/element_identifier.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/models/dialog_model.h"
#include "ui/compositor/layer_tree_owner.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/bubble/bubble_dialog_model_host.h"
#include "ui/views/controls/button/label_button.h"
#include "ui/views/controls/button/menu_button.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/menu/menu_runner.h"
#include "ui/views/view_utils.h"
#include "ui/views/widget/widget.h"

namespace uw {
namespace {

constexpr int kIndent = 16;
constexpr int kPadding = 8;
constexpr int kHeaderHeight = 32;
constexpr int kDisclosureWidth = 20;
DEFINE_LOCAL_ELEMENT_IDENTIFIER_VALUE(kTaskNameField);

class TaskNameDelegate : public ui::DialogModelDelegate {
 public:
  TaskNameDelegate(base::WeakPtr<TabOrganizationController> controller,
                   TreeId id)
      : controller_(controller), id_(std::move(id)) {}
  void Save() {
    if (controller_)
      controller_->Rename(
          id_, dialog_model()->GetTextfieldByUniqueId(kTaskNameField)->text());
  }

 private:
  base::WeakPtr<TabOrganizationController> controller_;
  TreeId id_;
};

}  // namespace

const tabs::TabInterface* FindFirstTab(const TabCollectionNode* node) {
  if (node->type() == TabCollectionNode::Type::TAB) {
    return std::get<tabs::ConstDanglingUntriagedTabInterface>(
               node->GetNodeData())
        .get();
  }
  for (const auto& child : node->children()) {
    if (const auto* tab = FindFirstTab(child.get()))
      return tab;
  }
  return nullptr;
}

const tabs::TabInterface* TabForNode(const TabCollectionNode* node) {
  return node->type() == TabCollectionNode::Type::TAB
             ? std::get<tabs::ConstDanglingUntriagedTabInterface>(
                   node->GetNodeData())
                   .get()
             : nullptr;
}

TabTreeAdapter::TabTreeAdapter(views::View* host, TabCollectionNode* collection)
    : host_(host), collection_(collection) {
  const auto* first_tab = FindFirstTab(collection);
  auto* controller = first_tab ? TabOrganizationController::From(
                                     first_tab->GetBrowserWindowInterface())
                               : nullptr;
  if (!controller)
    return;
  controller_ = controller->GetWeakPtr();
  organize_ = host_->AddChildView(std::make_unique<views::MenuButton>(
      base::BindRepeating(
          [](base::WeakPtr<TabTreeAdapter> self, const ui::Event&) {
            if (self)
              self->OpenMenu(self->organize_->GetBoundsInScreen().bottom_left(),
                             ui::mojom::MenuSourceType::kNone);
          },
          weak_factory_.GetWeakPtr()),
      l10n_util::GetStringUTF16(IDS_UW_ORGANIZE)));
  scratchpad_ = host_->AddChildView(std::make_unique<views::Label>(
      l10n_util::GetStringUTF16(IDS_UW_SCRATCHPAD)));
  scratchpad_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  subscription_ = controller->Observe(base::BindRepeating(
      &TabTreeAdapter::Refresh, weak_factory_.GetWeakPtr()));
  Refresh();
}

TabTreeAdapter::~TabTreeAdapter() {
  runner_.reset();
  for (const auto& [id, view] : tasks_)
    host_->RemoveChildViewT(view.get());
  for (const auto& [id, view] : disclosures_)
    host_->RemoveChildViewT(view.get());
  if (organize_)
    host_->RemoveChildViewT(organize_.get());
  if (scratchpad_)
    host_->RemoveChildViewT(scratchpad_.get());
}

void TabTreeAdapter::Refresh() {
  if (!controller_)
    return;
  const auto selection = controller_->SelectionFor();
  if (selection != last_tab_selection_)
    selected_task_.reset();
  last_tab_selection_ = selection;
  auto& tree = controller_->tree();
  std::set<TreeId> tasks;
  std::set<TreeId> parents;
  std::function<void(TabTree::Node*)> visit = [&](TabTree::Node* node) {
    const TreeId id = node->value.id;
    if (node->value.kind == TabTree::Kind::kTask) {
      tasks.insert(id);
      if (!tasks_.contains(id)) {
        auto button = std::make_unique<views::LabelButton>(
            base::BindRepeating(&TabTreeAdapter::SelectTask,
                                weak_factory_.GetWeakPtr(), id),
            node->GetTitle());
        button->SetHorizontalAlignment(gfx::ALIGN_LEFT);
        button->set_context_menu_controller(this);
        button->set_drag_controller(this);
        tasks_.emplace(id, host_->AddChildView(std::move(button)));
      }
      tasks_.at(id)->SetText(node->GetTitle());
      tasks_.at(id)->GetViewAccessibility().SetRole(ax::mojom::Role::kTreeItem);
      if (node->value.collapsed)
        tasks_.at(id)->GetViewAccessibility().SetIsCollapsed();
      else
        tasks_.at(id)->GetViewAccessibility().SetIsExpanded();
    }
    if ((node->value.kind == TabTree::Kind::kTask ||
         node->value.kind == TabTree::Kind::kPage) &&
        !node->children().empty()) {
      parents.insert(id);
      if (!disclosures_.contains(id)) {
        disclosures_.emplace(
            id, host_->AddChildView(std::make_unique<views::LabelButton>(
                    base::BindRepeating(&TabTreeAdapter::Toggle,
                                        weak_factory_.GetWeakPtr(), id),
                    u"")));
      }
      auto* button = disclosures_.at(id).get();
      button->SetText(node->value.collapsed ? u"\u25b8" : u"\u25be");
      button->GetViewAccessibility().SetName(l10n_util::GetStringUTF16(
          node->value.collapsed ? IDS_UW_EXPAND_BRANCH
                                : IDS_UW_COLLAPSE_BRANCH));
      button->SetTooltipText(node->GetTitle());
    }
    for (const auto& child : node->children())
      visit(child.get());
  };
  visit(tree.root());
  for (auto it = tasks_.begin(); it != tasks_.end();) {
    if (tasks.contains(it->first)) {
      ++it;
      continue;
    }
    host_->RemoveChildViewT(it->second.get());
    it = tasks_.erase(it);
  }
  for (auto it = disclosures_.begin(); it != disclosures_.end();) {
    if (parents.contains(it->first)) {
      ++it;
      continue;
    }
    host_->RemoveChildViewT(it->second.get());
    it = disclosures_.erase(it);
  }
  host_->InvalidateLayout();
  host_->PreferredSizeChanged();
}

views::ProposedLayout TabTreeAdapter::CalculateLayout(
    const views::SizeBounds& bounds) const {
  views::ProposedLayout layout;
  if (!controller_)
    return layout;
  rows_.clear();
  const int width = bounds.width().is_bounded() ? bounds.width().value() : 240;
  int y = 0;
  std::set<views::View*> placed;
  auto place = [&](views::View* view, bool visible, int x, int height) {
    gfx::Rect rect(x, y, std::max(0, width - x - kPadding),
                   visible ? height : 0);
    layout.child_layouts.emplace_back(view, visible, rect);
    placed.insert(view);
    return rect;
  };
  place(organize_, true, kPadding, kHeaderHeight);
  y += kHeaderHeight;
  std::map<TreeId, views::View*> tab_views;
  for (const auto& child : collection_->children()) {
    if (const auto* tab = TabForNode(child.get()))
      if (auto id = controller_->IdFor(tab))
        tab_views.emplace(*id, child->view());
  }
  const auto& tree = controller_->tree();
  std::function<void(const TabTree::Node*, int, bool)> visit =
      [&](const TabTree::Node* node, int depth, bool visible) {
        views::View* row = nullptr;
        const TreeId id = node->value.id;
        if (node->value.kind == TabTree::Kind::kScratchpad)
          row = scratchpad_;
        else if (auto task = tasks_.find(id); task != tasks_.end())
          row = task->second.get();
        else if (auto tab = tab_views.find(id); tab != tab_views.end())
          row = tab->second;
        const int indent = std::min(depth * kIndent, std::max(0, width - 100));
        if (row) {
          row->GetViewAccessibility().SetHierarchicalLevel(depth + 1);
          const int x = kPadding + indent;
          const int height = std::max(
              kHeaderHeight,
              row
                  ->GetPreferredSize(views::SizeBounds(
                      std::max(0, width - x - kDisclosureWidth - kPadding), {}))
                  .height());
          const auto rect = place(row, visible,
                                  node->value.kind == TabTree::Kind::kScratchpad
                                      ? x
                                      : x + kDisclosureWidth,
                                  height);
          if (auto disclosure = disclosures_.find(id);
              disclosure != disclosures_.end()) {
            layout.child_layouts.emplace_back(
                disclosure->second.get(), visible,
                gfx::Rect(x, y, kDisclosureWidth, height));
            placed.insert(disclosure->second.get());
          }
          if (visible) {
            rows_.push_back({id, gfx::Rect(x, y, width - x, height)});
            y += rect.height() + 2;
          }
        }
        const int child_depth =
            node->value.kind == TabTree::Kind::kScratchpad ? 0 : depth + 1;
        for (const auto& child : node->children())
          visit(child.get(), child_depth, visible && !node->value.collapsed);
      };
  for (const auto& node : tree.root()->children())
    visit(node.get(), 0, true);

  // Keep upstream group and split containers intact, including their drag
  // targets, focus behavior and tab indicators.
  for (auto* child : collection_->GetDirectChildren()) {
    if (placed.contains(child))
      continue;
    const int height =
        child->GetPreferredSize(views::SizeBounds(width - 2 * kPadding, {}))
            .height();
    place(child, true, kPadding, height);
    y += height + 2;
  }
  for (auto* child : host_->children()) {
    if (!placed.contains(child))
      layout.child_layouts.emplace_back(child, false, gfx::Rect());
  }
  layout.host_size = gfx::Size(width, y);
  return layout;
}

std::vector<TreeId> TabTreeAdapter::Selection() const {
  if (!controller_)
    return {};
  if (selected_task_ && controller_->tree().Find(*selected_task_))
    return {*selected_task_};
  return controller_->SelectionFor();
}

void TabTreeAdapter::SelectTask(TreeId id) {
  selected_task_ = id;
  if (auto it = tasks_.find(id); it != tasks_.end())
    it->second->RequestFocus();
}

void TabTreeAdapter::Toggle(TreeId id) {
  if (controller_)
    controller_->tree().ToggleCollapsed(id);
}

void TabTreeAdapter::OpenMenu(const gfx::Point& point,
                              ui::mojom::MenuSourceType source) {
  if (!controller_ || !host_->GetWidget())
    return;
  menu_selection_ = Selection();
  runner_.reset();
  menu_ = std::make_unique<ui::SimpleMenuModel>(this);
  menu_->AddItem(TabOrganizationController::kRenameTask,
                 l10n_util::GetStringUTF16(IDS_UW_RENAME_TASK));
  TabOrganizationController::AppendCommands(menu_.get());
  runner_ = std::make_unique<views::MenuRunner>(
      menu_.get(), views::MenuRunner::CONTEXT_MENU);
  runner_->RunMenuAt(host_->GetWidget(), nullptr, gfx::Rect(point, gfx::Size()),
                     views::MenuAnchorPosition::kTopLeft, source);
}

bool TabTreeAdapter::IsCommandIdEnabled(int command) const {
  return controller_ && controller_->CanExecute(command, menu_selection_);
}

void TabTreeAdapter::ExecuteCommand(int command, int) {
  if (!controller_ || !controller_->CanExecute(command, menu_selection_))
    return;
  if (command == TabOrganizationController::kRenameTask) {
    EditName(menu_selection_.front());
    return;
  }
  // Close branch can destroy the whole window. Test this weak pointer before
  // opening the name editor after a command.
  auto weak = weak_factory_.GetWeakPtr();
  const auto created = controller_->Execute(command, menu_selection_);
  if (weak && created) {
    selected_task_ = *created;
    EditName(*created);
  }
}

void TabTreeAdapter::EditName(TreeId task) {
  if (!controller_)
    return;
  const auto* node = controller_->tree().Find(task);
  if (!node || node->value.kind != TabTree::Kind::kTask)
    return;
  auto delegate = std::make_unique<TaskNameDelegate>(controller_, task);
  auto* raw_delegate = delegate.get();
  auto model = ui::DialogModel::Builder(std::move(delegate))
                   .SetTitle(l10n_util::GetStringUTF16(IDS_UW_RENAME_TASK))
                   .AddTextfield(kTaskNameField,
                                 l10n_util::GetStringUTF16(IDS_UW_TASK_NAME),
                                 node->GetTitle())
                   .AddOkButton(base::BindOnce(&TaskNameDelegate::Save,
                                               base::Unretained(raw_delegate)))
                   .AddCancelButton(base::DoNothing())
                   .Build();
  auto bubble = std::make_unique<views::BubbleDialogModelHost>(
      std::move(model), organize_, views::BubbleBorder::TOP_LEFT);
  views::BubbleDialogDelegate::CreateBubbleDeprecated(
      std::move(bubble), views::Widget::InitParams::WIDGET_OWNS_NATIVE_WIDGET)
      ->Show();
}

void TabTreeAdapter::ShowContextMenuForViewImpl(
    views::View* source,
    const gfx::Point& point,
    ui::mojom::MenuSourceType type) {
  for (const auto& [id, view] : tasks_) {
    if (view == source) {
      selected_task_ = id;
      break;
    }
  }
  OpenMenu(point, type);
}

std::optional<TabTreeAdapter::Destination> TabTreeAdapter::DropDestination(
    const gfx::Point& point) const {
  if (!controller_)
    return std::nullopt;
  const auto& tree = controller_->tree();
  for (const auto& row : rows_) {
    if (point.y() > row.bounds.bottom())
      continue;
    const auto* node = tree.Find(row.id);
    if (!node)
      continue;
    if (node->value.kind == TabTree::Kind::kScratchpad)
      return Destination{node->value.id, std::nullopt};
    // Middle third nests; edges insert before/after the target branch.
    if (point.y() > row.bounds.y() + row.bounds.height() / 3 &&
        point.y() < row.bounds.bottom() - row.bounds.height() / 3)
      return Destination{row.id, std::nullopt};
    if (point.y() <= row.bounds.CenterPoint().y())
      return Destination{node->parent()->value.id, row.id};
    const size_t next = node->parent()->GetIndexOf(node).value() + 1;
    const auto& siblings = node->parent()->children();
    return Destination{node->parent()->value.id,
                       next < siblings.size()
                           ? std::make_optional(siblings[next]->value.id)
                           : std::nullopt};
  }
  return Destination{tree.root()->value.id, std::nullopt};
}

void TabTreeAdapter::TrackNativeDrop(const gfx::Point& screen) {
  if (!controller_)
    return;
  auto target =
      DropDestination(views::View::ConvertPointFromScreen(host_, screen));
  if (target) {
    // Page tabs cannot be root siblings of Scratchpad/task sections.
    if (target->parent == controller_->tree().root()->value.id)
      target =
          Destination{controller_->tree().scratchpad()->value.id, std::nullopt};
    controller_->SetDropDestination(target->parent, target->before);
  }
}

std::optional<int> TabTreeAdapter::LinkDropIndex(
    const gfx::Point& point) const {
  if (!controller_)
    return std::nullopt;
  for (const auto& row : rows_) {
    if (point.y() <= row.bounds.bottom()) {
      if (auto* tab = controller_->TabFor(row.id))
        return tab->GetBrowserWindowInterface()
            ->GetTabStripModel()
            ->GetIndexOfTab(tab);
    }
  }
  return std::nullopt;
}

ui::ClipboardFormatType TabTreeAdapter::DragFormat() {
  return ui::ClipboardFormatType::CustomPlatformType("application/x-uw-task");
}

std::optional<TreeId> TabTreeAdapter::ReadDraggedTask(
    const ui::OSExchangeData& data) const {
  auto pickle = data.GetPickledData(DragFormat());
  if (!pickle || !controller_)
    return std::nullopt;
  std::string id;
  base::PickleIterator iterator(*pickle);
  if (!iterator.ReadString(&id))
    return std::nullopt;
  const auto* node = controller_->tree().Find(TreeId(id));
  return node && node->value.kind == TabTree::Kind::kTask
             ? std::make_optional(TreeId(id))
             : std::nullopt;
}

bool TabTreeAdapter::CanDrop(const ui::OSExchangeData& data) const {
  return ReadDraggedTask(data).has_value();
}

int TabTreeAdapter::DragUpdated(const ui::DropTargetEvent& event) {
  auto id = ReadDraggedTask(event.data());
  auto target = DropDestination(event.location());
  return id && target && controller_->tree().CanMove({*id}, target->parent)
             ? ui::DragDropTypes::DRAG_MOVE
             : ui::DragDropTypes::DRAG_NONE;
}

views::View::DropCallback TabTreeAdapter::DropCallback(
    const ui::DropTargetEvent& event) {
  auto id = ReadDraggedTask(event.data());
  auto target = DropDestination(event.location());
  if (!id || !target)
    return {};
  return base::BindOnce(&TabTreeAdapter::Drop, weak_factory_.GetWeakPtr(), *id,
                        *target);
}

void TabTreeAdapter::Drop(TreeId id,
                          Destination target,
                          const ui::DropTargetEvent&,
                          ui::mojom::DragOperation& operation,
                          std::unique_ptr<ui::LayerTreeOwner>) {
  if (controller_ && controller_->Move({id}, target.parent, target.before))
    operation = ui::mojom::DragOperation::kMove;
}

void TabTreeAdapter::WriteDragDataForView(views::View* sender,
                                          const gfx::Point&,
                                          ui::OSExchangeData* data) {
  for (const auto& [id, view] : tasks_) {
    if (view != sender)
      continue;
    base::Pickle pickle;
    pickle.WriteString(id.value());
    data->SetPickledData(DragFormat(), pickle);
    return;
  }
}

int TabTreeAdapter::GetDragOperationsForView(views::View* sender,
                                             const gfx::Point&) {
  for (const auto& [id, view] : tasks_) {
    if (view == sender)
      return ui::DragDropTypes::DRAG_MOVE;
  }
  return ui::DragDropTypes::DRAG_NONE;
}

bool TabTreeAdapter::CanStartDragForView(views::View*,
                                         const gfx::Point& press,
                                         const gfx::Point& current) {
  return views::View::ExceededDragThreshold(current - press);
}

}  // namespace uw
