// Copyright 2026 The uw Authors. All rights reserved.

#ifndef UW_BROWSER_TAB_ORGANIZATION_HIDDEN_TABS_H_
#define UW_BROWSER_TAB_ORGANIZATION_HIDDEN_TABS_H_

#include "ui/base/unowned_user_data/scoped_unowned_user_data.h"

namespace tabs {
class TabInterface;
}

namespace uw {

// Says which tabs of a window a collapsed tab branch hides. Chromium's tab
// strip asks it wherever it asks whether a tab is in a collapsed tab group,
// and cannot depend on the tab tree that answers.
class HiddenTabs {
 public:
  DECLARE_USER_DATA(HiddenTabs);

  HiddenTabs(const HiddenTabs&) = delete;
  HiddenTabs& operator=(const HiddenTabs&) = delete;

  virtual bool IsHidden(const tabs::TabInterface* tab) const = 0;

 protected:
  explicit HiddenTabs(ui::UnownedUserDataHost& host);
  virtual ~HiddenTabs();

 private:
  ui::ScopedUnownedUserData<HiddenTabs> user_data_;
};

}  // namespace uw

#endif  // UW_BROWSER_TAB_ORGANIZATION_HIDDEN_TABS_H_
