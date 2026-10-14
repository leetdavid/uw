// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#include "uw/browser/tab_organization/commands.h"

#include "uw/browser/tab_organization/tab_organization_controller.h"

namespace uw {

bool IsWindowCommand(int command) {
  return TabOrganizationController::IsCommand(command) ||
         (command >= kGoToTask1 && command <= kLastWindowCommand);
}

}  // namespace uw
