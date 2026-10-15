// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/browser/ui/views/tab_organization/tab_indent.h"

#include <algorithm>
#include <variant>

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

constexpr int kIndentPerLevel = 16;
constexpr int kMaxIndentLevels = 6;

TabOrganizationController* ControllerFor(TabCollectionNode* node) {
  TabStripCollectionController* strip = node ? node->GetController() : nullptr;
  return strip ? TabOrganizationController::From(
                     strip->GetBrowserView()->browser())
               : nullptr;
}

// The tab `child` shows, or null when it shows something else.
const tabs::TabInterface* TabShownBy(TabCollectionNode* parent,
                                     const views::View* child) {
  if (!parent) {
    return nullptr;
  }
  for (const auto& node : parent->children()) {
    if (node->view() == child && node->type() == TabCollectionNode::Type::TAB) {
      return std::get<tabs::ConstDanglingUntriagedTabInterface>(
                 node->GetNodeData())
          .get();
    }
  }
  return nullptr;
}

}  // namespace

int TabIndentFor(TabCollectionNode* parent, const views::View* child) {
  const tabs::TabInterface* tab = TabShownBy(parent, child);
  const TabOrganizationController* controller = ControllerFor(parent);
  return tab && controller
             ? std::min(controller->DepthFor(tab), kMaxIndentLevels) *
                   kIndentPerLevel
             : 0;
}

bool IsHiddenInBranch(TabCollectionNode* parent, const views::View* child) {
  const tabs::TabInterface* tab = TabShownBy(parent, child);
  const TabOrganizationController* controller = ControllerFor(parent);
  return tab && controller && controller->IsHidden(tab);
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
