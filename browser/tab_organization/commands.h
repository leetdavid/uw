// Copyright 2026 The uw Authors
// Use of this source code is governed by a BSD-style license.

#ifndef UW_BROWSER_TAB_ORGANIZATION_COMMANDS_H_
#define UW_BROWSER_TAB_ORGANIZATION_COMMANDS_H_

namespace uw {

// Commands of the menu bar and keys that act on the window's selected tab,
// its task, or automatic organization. The numbers follow
// TabOrganizationController::Command, whose commands also run as window
// commands, on the selected tabs.
enum WindowCommand {
  kGoToTask1 = 56020,
  kGoToTask9 = kGoToTask1 + 8,
  kGoToScratchpad,
  kRenameTask,
  kSuggestTaskName,
  kConfirmSuggestion,
  kPauseOrganizationForAnHour,
  kPauseOrganizationUntilLaunch,
  kPauseOrganizationIndefinitely,
  kResumeOrganization,
  kShowOrganizerActivity,
  kShowOrganizationSettings,
  kLastWindowCommand = kShowOrganizationSettings,
};

bool IsWindowCommand(int command);

}  // namespace uw

#endif  // UW_BROWSER_TAB_ORGANIZATION_COMMANDS_H_
