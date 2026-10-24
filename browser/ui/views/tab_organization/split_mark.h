// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#ifndef UW_BROWSER_UI_VIEWS_TAB_ORGANIZATION_SPLIT_MARK_H_
#define UW_BROWSER_UI_VIEWS_TAB_ORGANIZATION_SPLIT_MARK_H_

#include "components/tabs/public/tab_interface.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/views/controls/image_view.h"

namespace uw {

// The split icon at the end of the top row of a side-by-side pair in the
// vertical sidebar. It gives way to the close button while that row is
// selected or under the pointer.
class SplitMark : public views::ImageView {
  METADATA_HEADER(SplitMark, views::ImageView)

 public:
  explicit SplitMark(tabs::TabHandle tab);
  SplitMark(const SplitMark&) = delete;
  SplitMark& operator=(const SplitMark&) = delete;
  ~SplitMark() override;

  // Whether the row shows the mark, given whether it is under the pointer.
  bool ShownOnRow(bool hovered) const;

  // views::ImageView:
  void OnThemeChanged() override;

 private:
  const tabs::TabHandle tab_;
};

}  // namespace uw

#endif  // UW_BROWSER_UI_VIEWS_TAB_ORGANIZATION_SPLIT_MARK_H_
