// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#ifndef UW_BROWSER_UI_VIEWS_TAB_ORGANIZATION_KEPT_MARK_H_
#define UW_BROWSER_UI_VIEWS_TAB_ORGANIZATION_KEPT_MARK_H_

#include "base/callback_list.h"
#include "components/tabs/public/tab_interface.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/views/controls/button/image_button.h"

namespace uw {

class TabOrganizationController;

// The anchor at the end of a kept tab's row in the vertical sidebar. A click
// lets the tab go, as Stop Keeping Here does.
class KeptMark : public views::ImageButton {
  METADATA_HEADER(KeptMark, views::ImageButton)

 public:
  explicit KeptMark(tabs::TabHandle tab);
  KeptMark(const KeptMark&) = delete;
  KeptMark& operator=(const KeptMark&) = delete;
  ~KeptMark() override;

  bool kept() const { return kept_; }

  // views::ImageButton:
  void AddedToWidget() override;
  void RemovedFromWidget() override;
  void OnThemeChanged() override;

 private:
  TabOrganizationController* Controller() const;
  void Refresh();
  void LetGo();

  const tabs::TabHandle tab_;
  bool kept_ = false;
  base::CallbackListSubscription changed_;
};

}  // namespace uw

#endif  // UW_BROWSER_UI_VIEWS_TAB_ORGANIZATION_KEPT_MARK_H_
