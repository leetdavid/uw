// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#ifndef UW_BROWSER_UI_VIEWS_TAB_ORGANIZATION_SUGGEST_NAME_ITEM_H_
#define UW_BROWSER_UI_VIEWS_TAB_ORGANIZATION_SUGGEST_NAME_ITEM_H_

#include <string>

#include "base/callback_list.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "components/tab_groups/tab_group_id.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/views/view.h"

class BrowserWindowInterface;

namespace views {
class LabelButton;
}

namespace uw {

class AutomaticOrganizer;

// The "Suggest a name" row of a task's menu. A click asks the language model
// to name the task from the tabs it holds. The row says so while the request
// is out, and says when no name came of it. It is hidden when there is no
// language model to ask, and in a private window.
class SuggestNameItem : public views::View {
  METADATA_HEADER(SuggestNameItem, views::View)

 public:
  // `insets` are those of the other rows of the menu.
  SuggestNameItem(BrowserWindowInterface* window,
                  const tab_groups::TabGroupId& group,
                  const gfx::Insets& insets);
  SuggestNameItem(const SuggestNameItem&) = delete;
  SuggestNameItem& operator=(const SuggestNameItem&) = delete;
  ~SuggestNameItem() override;

  views::LabelButton* button() { return button_; }

 private:
  void Suggest();
  void OnFailed(const std::u16string& reason);
  void Refresh();

  const tab_groups::TabGroupId group_;
  // The window owns the organizer, and the menu can outlive the window.
  base::WeakPtr<AutomaticOrganizer> organizer_;
  raw_ptr<views::LabelButton> button_ = nullptr;
  // Set when the last request gave no name, with the reason.
  bool failed_ = false;
  std::u16string reason_;
  base::CallbackListSubscription naming_;
  base::WeakPtrFactory<SuggestNameItem> weak_factory_{this};
};

}  // namespace uw

#endif  // UW_BROWSER_UI_VIEWS_TAB_ORGANIZATION_SUGGEST_NAME_ITEM_H_
