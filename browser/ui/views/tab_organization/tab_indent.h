// Copyright 2026 The uw Authors. All rights reserved.

#ifndef UW_BROWSER_UI_VIEWS_TAB_ORGANIZATION_TAB_INDENT_H_
#define UW_BROWSER_UI_VIEWS_TAB_ORGANIZATION_TAB_INDENT_H_

#include "base/callback_list.h"

class TabCollectionNode;

namespace views {
class View;
}

namespace uw {

// Horizontal inset for `child`, the view of one of `parent`'s child nodes,
// when it is a tab nested under other pages. 0 for anything else.
int TabIndentFor(TabCollectionNode* parent, const views::View* child);

// True when `child`, the view of one of `parent`'s child nodes, is a tab that
// a collapsed tab branch hides.
bool IsHiddenInBranch(TabCollectionNode* parent, const views::View* child);

// True when `child`, the view of one of `parent`'s child nodes, shows its
// window's selected tab. A task folded in another window keeps that row.
bool ShowsSelectedTab(TabCollectionNode* parent, const views::View* child);

// Lays out `container` and its child views again whenever the window's page
// tree changes. `container` must own the returned subscription.
base::CallbackListSubscription RelayoutOnTreeChange(TabCollectionNode* node,
                                                    views::View* container);

}  // namespace uw

#endif  // UW_BROWSER_UI_VIEWS_TAB_ORGANIZATION_TAB_INDENT_H_
