// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/browser/ui/views/tab_organization/tab_indent.h"

#include <algorithm>
#include <variant>
#include <vector>

#include "base/functional/bind.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/tabs/common/tab_collection_node.h"
#include "chrome/browser/ui/views/tabs/common/tab_strip_collection_controller.h"
#include "components/tabs/public/tab_collection_types.h"
#include "components/tabs/public/tab_interface.h"
#include "ui/views/view.h"
#include "uw/browser/tab_organization/tab_organization_controller.h"

namespace uw {
namespace {

TabOrganizationController* ControllerFor(TabCollectionNode* node) {
  TabStripCollectionController* strip = node ? node->GetController() : nullptr;
  return strip ? TabOrganizationController::From(
                     strip->GetBrowserView()->browser())
               : nullptr;
}

const tabs::TabInterface* TabOf(const TabCollectionNode& node) {
  return std::get<tabs::ConstDanglingUntriagedTabInterface>(
             node.GetNodeData())
      .get();
}

// The tabs `child` shows: one for a tab's row, the pair for a split's row,
// and none for anything else.
std::vector<const tabs::TabInterface*> TabsShownBy(TabCollectionNode* parent,
                                                   const views::View* child) {
  std::vector<const tabs::TabInterface*> shown;
  if (!parent) {
    return shown;
  }
  for (const auto& node : parent->children()) {
    if (node->view() != child) {
      continue;
    }
    if (node->type() == TabCollectionNode::Type::TAB) {
      shown.push_back(TabOf(*node));
    } else if (node->type() == TabCollectionNode::Type::SPLIT) {
      for (const auto& half : node->children()) {
        if (half->type() == TabCollectionNode::Type::TAB) {
          shown.push_back(TabOf(*half));
        }
      }
    }
  }
  return shown;
}

}  // namespace

int TabIndentFor(TabCollectionNode* parent, const views::View* child) {
  const std::vector<const tabs::TabInterface*> shown =
      TabsShownBy(parent, child);
  const TabOrganizationController* controller = ControllerFor(parent);
  if (shown.empty() || !controller) {
    return 0;
  }
  // A split's row sits where the shallower of its pair would.
  int depth = kMaxShownDepth;
  for (const tabs::TabInterface* tab : shown) {
    depth = std::min(depth, controller->DepthFor(tab));
  }
  return depth * kIndentPerLevel;
}

bool IsHiddenInBranch(TabCollectionNode* parent, const views::View* child) {
  const std::vector<const tabs::TabInterface*> shown =
      TabsShownBy(parent, child);
  const TabOrganizationController* controller = ControllerFor(parent);
  return !shown.empty() && controller &&
         std::ranges::all_of(shown, [&](const tabs::TabInterface* tab) {
           return controller->IsHidden(tab);
         });
}

bool ShowsSelectedTab(TabCollectionNode* parent, const views::View* child) {
  return std::ranges::any_of(
      TabsShownBy(parent, child),
      [](const tabs::TabInterface* tab) { return tab->IsActivated(); });
}

base::CallbackListSubscription RelayoutOnTreeChange(TabCollectionNode* node,
                                                    views::View* container) {
  TabOrganizationController* controller = ControllerFor(node);
  if (!controller) {
    return {};
  }
  return controller->Observe(base::BindRepeating(
      [](views::View* container) {
        for (views::View* child : container->children()) {
          child->InvalidateLayout();
        }
        container->InvalidateLayout();
      },
      base::Unretained(container)));
}

}  // namespace uw
