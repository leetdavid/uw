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
#include "base/time/time.h"
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
#include "ui/base/mojom/menu_source_type.mojom.h"
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
#include "uw/browser/ui/views/tab_organization/ai_setup_dialog.h"

namespace uw {
namespace {

constexpr int kIndent = 16;
constexpr int kPadding = 8;
constexpr int kHeaderHeight = 32;
constexpr int kDisclosureWidth = 20;
// Organize-menu commands for provider setup and automatic-organization pause
// controls. Outside the controller's undoable tree-command range.
constexpr int kSetupAutomatic = 57000;
constexpr int kPause1Hour = 57001;
constexpr int kPauseUntilLaunch = 57002;
constexpr int kPauseIndefinite = 57003;
constexpr int kResume = 57004;
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

const tabs::TabInterface* TabForNode(const TabCollectionNode* node) {
  return node->type() == TabCollectionNode::Type::TAB
             ? std::get<tabs::ConstDanglingUntriagedTabInterface>(
                   node->GetNodeData())
                   .get()
             : nullptr;
}

TabTreeAdapter::TabTreeAdapter(views::View* host,
                               TabCollectionNode* collection,
                               TabOrganizationController* controller)
    : host_(host), collection_(collection) {
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
  organize_->SetTextSubpixelRenderingEnabled(false);
  scratchpad_ = host_->AddChildView(std::make_unique<views::Label>(
      l10n_util::GetStringUTF16(IDS_UW_SCRATCHPAD)));
  scratchpad_->SetSubpixelRenderingEnabled(false);
  scratchpad_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  subscription_ = controller->Observe(base::BindRepeating(
      &TabTreeAdapter::Refresh, weak_factory_.GetWeakPtr()));
  Refresh();
}

TabTreeAdapter::~TabTreeAdapter() {
  runner_.reset();
  notice_timer_.Stop();
  std::vector<views::View*> owned;
  owned.reserve(tasks_.size() + disclosures_.size() + 3);
  for (const auto& [id, view] : tasks_)
    owned.push_back(view.get());
  tasks_.clear();
  for (const auto& [id, view] : disclosures_)
    owned.push_back(view.get());
  disclosures_.clear();
  if (organize_) {
    owned.push_back(organize_.get());
    organize_ = nullptr;
  }
  if (scratchpad_) {
    owned.push_back(scratchpad_.get());
    scratchpad_ = nullptr;
  }
  if (notice_) {
    owned.push_back(notice_.get());
    notice_ = nullptr;
  }
  for (auto* view : owned)
    RemoveOwnedView(view);
}

void TabTreeAdapter::RemoveOwnedView(views::View* view) {
  removing_.insert(view);
  host_->RemoveChildViewT(view);
  removing_.erase(view);
}

void TabTreeAdapter::ShowOrganizationNotice(
    TabOrganizationController::Notice notice) {
  ClearNotice();
  if (notice.text.empty() || !controller_) {
    return;
  }
  std::unique_ptr<views::View> view;
  if (notice.undoable) {
    auto button = std::make_unique<views::LabelButton>(
        base::BindRepeating(&TabTreeAdapter::OnNoticeActivated,
                            weak_factory_.GetWeakPtr()),
        notice.text + u" \u2014 " +
            l10n_util::GetStringUTF16(IDS_UW_AUTO_UNDO));
    button->SetTextSubpixelRenderingEnabled(false);
    button->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    view = std::move(button);
  } else {
    auto label = std::make_unique<views::Label>(std::move(notice.text));
    label->SetSubpixelRenderingEnabled(false);
    label->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    view = std::move(label);
  }
  notice_ = host_->AddChildView(std::move(view));
  notice_timer_.Start(FROM_HERE, base::Seconds(10),
                      base::BindOnce(&TabTreeAdapter::ClearNotice,
                                     base::Unretained(this)));
  host_->InvalidateLayout();
}

void TabTreeAdapter::ClearNotice() {
  notice_timer_.Stop();
  if (!notice_) {
    return;
  }
  auto* notice = notice_.get();
  notice_ = nullptr;
  RemoveOwnedView(notice);
  host_->InvalidateLayout();
}

void TabTreeAdapter::OnNoticeActivated() {
  if (controller_) {
    controller_->Undo();
  }
  ClearNotice();
}

void TabTreeAdapter::Refresh() {
  if (!controller_)
    return;
  if (auto notice = controller_->TakeNotice()) {
    ShowOrganizationNotice(std::move(*notice));
  }
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
        button->SetTextSubpixelRenderingEnabled(false);
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
      button->SetTextSubpixelRenderingEnabled(false);
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
  // Stale views are hidden, not destroyed: the host's animating layout
  // manager retains raw pointers to laid-out children across animation
  // frames, so freeing a view mid-life trips the dangling-pointer detector
  // even after the recalculated target drops it. Hidden views stay alive
  // (and invisible to events, layout, and accessibility) until adapter
  // teardown, whose removal path the browser tests cover. Deletions only
  // happen for discarded tasks, so the retained set stays small.
  for (auto it = tasks_.begin(); it != tasks_.end();) {
    if (tasks.contains(it->first)) {
      ++it;
      continue;
    }
    it->second->SetVisible(false);
    it = tasks_.erase(it);
  }
  for (auto it = disclosures_.begin(); it != disclosures_.end();) {
    if (parents.contains(it->first)) {
      ++it;
      continue;
    }
    it->second->SetVisible(false);
    it = disclosures_.erase(it);
  }
  host_->InvalidateLayout();
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
  if (notice_) {
    place(notice_.get(), true, kPadding, kHeaderHeight);
    y += kHeaderHeight;
  }
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
  for (const auto& child : host_->children()) {
    // Views mid-removal are still attached while the layout manager
    // processes its synchronous removal notification. Omitting them keeps
    // the recalculated target free of soon-destroyed views.
    if (!placed.contains(child) && !removing_.contains(child))
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
  menu_->AddSeparator(ui::NORMAL_SEPARATOR);
  menu_->AddItem(kSetupAutomatic,
                 l10n_util::GetStringUTF16(IDS_UW_AUTO_SETUP));
  menu_->AddSeparator(ui::NORMAL_SEPARATOR);
  menu_->AddItem(kPause1Hour,
                 l10n_util::GetStringUTF16(IDS_UW_PAUSE_1HOUR));
  menu_->AddItem(kPauseUntilLaunch,
                 l10n_util::GetStringUTF16(IDS_UW_PAUSE_UNTIL_LAUNCH));
  menu_->AddItem(kPauseIndefinite,
                 l10n_util::GetStringUTF16(IDS_UW_PAUSE_INDEFINITE));
  menu_->AddItem(kResume, l10n_util::GetStringUTF16(IDS_UW_PAUSE_RESUME));
  runner_ = std::make_unique<views::MenuRunner>(
      menu_.get(), views::MenuRunner::CONTEXT_MENU);
  runner_->RunMenuAt(host_->GetWidget(), nullptr, gfx::Rect(point, gfx::Size()),
                     views::MenuAnchorPosition::kTopLeft, source);
}

bool TabTreeAdapter::IsCommandIdEnabled(int command) const {
  if (command == kSetupAutomatic || command == kPause1Hour ||
      command == kPauseUntilLaunch || command == kPauseIndefinite) {
    return controller_ != nullptr;
  }
  if (command == kResume) {
    return controller_ && controller_->pause_mode() != PauseMode::kOff;
  }
  return controller_ && controller_->CanExecute(command, menu_selection_);
}

void TabTreeAdapter::ExecuteCommand(int command, int) {
  if (!controller_) {
    return;
  }
  if (command == kSetupAutomatic) {
    AiSetupDialog::Show(organize_, controller_->GetWeakPtr());
    return;
  }
  if (command == kPause1Hour) {
    controller_->SetPause(PauseMode::kTimed, base::Hours(1));
    return;
  }
  if (command == kPauseUntilLaunch) {
    controller_->SetPause(PauseMode::kUntilLaunch);
    return;
  }
  if (command == kPauseIndefinite) {
    controller_->SetPause(PauseMode::kIndefinite);
    return;
  }
  if (command == kResume) {
    controller_->SetPause(PauseMode::kOff);
    return;
  }
  if (!controller_->CanExecute(command, menu_selection_)) {
    return;
  }
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
    // Horizontal displacement pulls the branch in or out of nesting levels:
    // dragging right nests under the anchor row, dragging left outdents past
    // ancestors (one level per indent width). Near the row's own indent the
    // existing vertical behavior applies.
    const int dx = point.x() - row.bounds.x();
    const bool after = point.y() > row.bounds.CenterPoint().y();
    if (dx >= kIndent / 2 || dx <= -kIndent / 2) {
      const int levels = dx > 0 ? 1 : -std::max(1, -dx / kIndent);
      if (auto target = tree.DropTargetForAnchor(row.id, levels, after))
        return Destination{target->parent, target->before};
    }
    // Middle third nests; edges insert before/after the target branch.
    if (point.y() > row.bounds.y() + row.bounds.height() / 3 &&
        point.y() < row.bounds.bottom() - row.bounds.height() / 3)
      return Destination{row.id, std::nullopt};
    if (!after)
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
