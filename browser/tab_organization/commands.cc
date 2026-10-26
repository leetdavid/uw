// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/browser/tab_organization/commands.h"

#include "uw/browser/tab_organization/tab_organization_controller.h"

namespace uw {

bool IsWindowCommand(int command) {
  return TabOrganizationController::IsCommand(command) ||
         (command >= kGoToTask1 && command <= kLastWindowCommand);
}

}  // namespace uw
