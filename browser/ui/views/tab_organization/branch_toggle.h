// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#ifndef UW_BROWSER_UI_VIEWS_TAB_ORGANIZATION_BRANCH_TOGGLE_H_
#define UW_BROWSER_UI_VIEWS_TAB_ORGANIZATION_BRANCH_TOGGLE_H_

#include "base/callback_list.h"
#include "components/tabs/public/tab_interface.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/views/controls/button/image_button.h"
#include "ui/views/controls/label.h"

namespace views {
struct ProposedLayout;
}

namespace uw {

class TabOrganizationController;

// A chevron that collapses or expands the branch of a tab with tabs under
// it. It takes the place of the tab's favicon while the pointer is on the
// tab's row.
class BranchToggle : public views::ImageButton {
  METADATA_HEADER(BranchToggle, views::ImageButton)

 public:
  explicit BranchToggle(tabs::TabHandle tab);
  BranchToggle(const BranchToggle&) = delete;
  BranchToggle& operator=(const BranchToggle&) = delete;
  ~BranchToggle() override;

  // Tabs under this tab, hidden or not.
  int descendants() const { return descendants_; }
  bool collapsed() const { return collapsed_; }

  // views::ImageButton:
  void AddedToWidget() override;
  void RemovedFromWidget() override;
  void OnThemeChanged() override;

 private:
  TabOrganizationController* Controller() const;
  void Refresh();
  void UpdateImage();
  void Toggle();

  const tabs::TabHandle tab_;
  int descendants_ = 0;
  bool collapsed_ = false;
  base::CallbackListSubscription changed_;
};

// How many tabs a collapsed branch hides, at the end of its top tab's row.
class HiddenCount : public views::Label {
  METADATA_HEADER(HiddenCount, views::Label)

 public:
  HiddenCount();
  HiddenCount(const HiddenCount&) = delete;
  HiddenCount& operator=(const HiddenCount&) = delete;
  ~HiddenCount() override;

  void SetCount(int count);

  // views::Label:
  void OnThemeChanged() override;
};

// Adds a BranchToggle, a HiddenCount, a ChatMark, a KeptMark, and a SplitMark
// to `row`, the vertical tab row of `tab`.
void AddBranchControls(views::View* row, tabs::TabHandle tab);

// Adds the controls of the row that holds `icon` and `title` to `layout`,
// the row's layout. The toggle covers the icon while the row is `hovered`.
// The count, the chat's mark, a split's icon, and a kept tab's anchor end
// the title and shorten it.
void LayOutBranchControls(views::View* icon,
                          views::View* title,
                          bool hovered,
                          views::ProposedLayout& layout);

}  // namespace uw

#endif  // UW_BROWSER_UI_VIEWS_TAB_ORGANIZATION_BRANCH_TOGGLE_H_
