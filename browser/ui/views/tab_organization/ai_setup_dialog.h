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

// Modal setup bubble for the automatic-organization connection. Save writes
// the mode, endpoint, and model choice to profile preferences, stores the API
// key in Keychain (OpenAI-compatible mode only), and asks the controller to
// validate the connection before enabling organization. Local validation
// failures save nothing. The dialog shows the exact request scope before the
// user enables anything.
class AiSetupDialog {
 public:
  static void Show(views::View* anchor,
                   base::WeakPtr<TabOrganizationController> controller);
  AiSetupDialog(const AiSetupDialog&) = delete;
  AiSetupDialog& operator=(const AiSetupDialog&) = delete;
};

}  // namespace uw

#endif  // UW_BROWSER_UI_VIEWS_TAB_ORGANIZATION_AI_SETUP_DIALOG_H_
