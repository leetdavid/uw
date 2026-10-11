// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/browser/ui/views/tab_organization/new_task_button.h"

#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "chrome/grit/generated_resources.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/views/controls/button/button.h"
#include "uw/browser/tab_organization/tab_organization_controller.h"

namespace uw {

void OpenNewTaskFrom(views::Button* button, BrowserWindowInterface* window) {
  button->SetCallback(base::BindRepeating(
      base::IgnoreResult(&TabOrganizationController::OpenNewTask), window));
  button->SetTooltipText(l10n_util::GetStringUTF16(IDS_UW_OPEN_NEW_TASK));
}

}  // namespace uw
