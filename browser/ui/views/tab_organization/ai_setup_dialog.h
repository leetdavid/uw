// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#ifndef UW_BROWSER_UI_VIEWS_TAB_ORGANIZATION_AI_SETUP_DIALOG_H_
#define UW_BROWSER_UI_VIEWS_TAB_ORGANIZATION_AI_SETUP_DIALOG_H_

#include "base/memory/weak_ptr.h"

namespace views {
class View;
}

namespace uw {

class TabOrganizationController;

// Modal setup bubble for the two automatic-organization providers. Save
// writes endpoints and model choice to profile prefs, stores keys in
// Keychain, and asks the controller to validate both providers before
// enabling organization. Local validation failures save nothing.
class AiSetupDialog {
 public:
  static void Show(views::View* anchor,
                   base::WeakPtr<TabOrganizationController> controller);
  AiSetupDialog(const AiSetupDialog&) = delete;
  AiSetupDialog& operator=(const AiSetupDialog&) = delete;
};

}  // namespace uw

#endif  // UW_BROWSER_UI_VIEWS_TAB_ORGANIZATION_AI_SETUP_DIALOG_H_
