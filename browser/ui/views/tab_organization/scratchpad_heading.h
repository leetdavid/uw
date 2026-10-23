// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#ifndef UW_BROWSER_UI_VIEWS_TAB_ORGANIZATION_SCRATCHPAD_HEADING_H_
#define UW_BROWSER_UI_VIEWS_TAB_ORGANIZATION_SCRATCHPAD_HEADING_H_

#include <vector>

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/views/view.h"
#include "uw/components/tab_organization/page_tree.h"

namespace views {
class Label;
class LabelButton;
}  // namespace views

namespace uw {

class TabOrganizationController;

// Height the Scratchpad heading takes above the first ungrouped tab.
inline constexpr int kScratchpadHeadingHeight = 24;

// The first of `children`, the views of the unpinned tab container in order,
// that is a tab in no task. The Scratchpad starts there, below every task.
// Null when no tab is ungrouped.
const views::View* FirstScratchpadTab(
    const std::vector<views::View*>& children);

// The "Scratchpad" label above the first tab in no task. While the pointer is
// on it, Clear shows at its end and closes the Scratchpad's tabs, except kept
// tabs and terminals running a command.
class ScratchpadHeading : public views::View {
  METADATA_HEADER(ScratchpadHeading, views::View)

 public:
  ScratchpadHeading();
  ScratchpadHeading(const ScratchpadHeading&) = delete;
  ScratchpadHeading& operator=(const ScratchpadHeading&) = delete;
  ~ScratchpadHeading() override;

  views::LabelButton* clear_button() { return clear_; }

  // views::View:
  void OnMouseEntered(const ui::MouseEvent& event) override;
  void OnMouseExited(const ui::MouseEvent& event) override;
  void OnThemeChanged() override;

 private:
  TabOrganizationController* Controller() const;
  void Clear();
  void ShowCount(std::vector<PageId> clearable);

  raw_ptr<views::Label> label_ = nullptr;
  raw_ptr<views::LabelButton> clear_ = nullptr;
  base::WeakPtrFactory<ScratchpadHeading> weak_factory_{this};
};

}  // namespace uw

#endif  // UW_BROWSER_UI_VIEWS_TAB_ORGANIZATION_SCRATCHPAD_HEADING_H_
